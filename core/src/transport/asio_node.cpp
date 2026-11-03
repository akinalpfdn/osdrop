#include "transport/asio_node.hpp"

#include "protocol/constants.hpp"
#include "transport/traffic_class.hpp"

// CMake includes Asio as a system header; SwiftPM cannot, so its warnings are silenced here.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif
#include <asio/connect.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>
#include <asio/ip/v6_only.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <exception>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace osdrop::transport {

namespace {

using asio::ip::tcp;
using asio::ip::udp;

/// Large enough for a whole transfer frame in one read.
constexpr std::size_t kReadBufferBytes = std::size_t{64} * 1024;
constexpr auto kConnectTimeout = std::chrono::seconds(5);
constexpr auto kShutdownGrace = std::chrono::seconds(1);
constexpr auto kCommandTimeout = std::chrono::seconds(5);
/// The timer never sleeps longer, so a wrong next_wakeup cannot stall the mesh for long.
constexpr auto kMaxTimerWait = std::chrono::seconds(1);
constexpr int kListenBacklog = 16;

TimePoint now() {
    return Clock::now();
}

} // namespace

class AsioNode::Impl final : public mesh::MeshTransport {
public:
    /// One TCP connection (control or transfer) and, for a session, its input socket. Handlers
    /// hold it by shared_ptr, so its buffers outlive every operation in flight.
    struct Connection {
        explicit Connection(asio::io_context& io, mesh::ConnectionId connection_id)
            : id(connection_id), socket(io), resolver(io), connect_timer(io), input(io) {}

        mesh::ConnectionId id;
        tcp::socket socket;
        tcp::resolver resolver;
        asio::steady_timer connect_timer;
        bool connect_done = false;
        udp::socket input;
        std::optional<udp::endpoint> input_peer;
        udp::endpoint datagram_source;
        std::deque<std::vector<std::uint8_t>> queue;
        std::size_t queued = 0;
        std::size_t write_offset = 0;
        bool writing = false;
        /// Closed by the mesh or by the peer: nothing more goes to the mesh.
        bool closing = false;
        std::array<std::uint8_t, kReadBufferBytes> read_buffer{};
        std::array<std::uint8_t, protocol::kMaxDatagramSize + 1> datagram_buffer{};
    };
    using ConnectionPtr = std::shared_ptr<Connection>;

    Impl(mesh::MeshConfig config, crypto::KeyPair identity, crypto::RandomSource& random,
         mesh::MeshObserver& observer)
        : work_(asio::make_work_guard(io_)), acceptor_(io_), timer_(io_),
          mesh_(std::move(config), std::move(identity), random, observer, *this) {}

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    ~Impl() override {
        try {
            stop();
        } catch (...) {
            // Only joining the I/O thread can throw; a node that cannot stop its thread is
            // unrecoverable, and this makes the implicit terminate explicit.
            std::terminate();
        }
    }

    // --- Setup (before the I/O thread starts) ---

    Status<Error> listen(std::uint16_t port) {
        asio::error_code error;
        if ((port == 0 || !open_acceptor(port, error)) && !open_acceptor(0, error)) {
            return Status<Error>::failure(Error::NetworkError);
        }
        listen_port_ = acceptor_.local_endpoint(error).port();
        return error ? Status<Error>::failure(Error::NetworkError) : Status<Error>::ok();
    }

    void start() {
        accept_next();
        reschedule();
        thread_ = std::thread([this] {
            try {
                io_.run();
            } catch (...) {
                // An exception from a handler means memory ran out or Asio failed internally;
                // neither leaves the sessions in a state worth continuing, so the process stops
                // explicitly instead of losing its I/O thread silently.
                std::terminate();
            }
            finished_.set_value();
        });
    }

    [[nodiscard]] std::uint16_t port() const { return listen_port_; }
    [[nodiscard]] mesh::DeviceId device_id() const { return mesh_.device_id(); }

    // --- Commands (any thread) ---

    template <typename Work>
    void post(Work&& work) {
        asio::post(io_, [this, moved = std::forward<Work>(work)]() mutable {
            moved(mesh_, now());
            reschedule();
        });
    }

    /// Runs `work` on the I/O thread and waits for its result.
    template <typename Result, typename Work>
    Result run_sync(Work&& work, Result if_stopped) {
        auto task = std::make_shared<std::packaged_task<Result()>>(
            [this, moved = std::forward<Work>(work)]() mutable {
                Result result = moved(mesh_, now());
                reschedule();
                return result;
            });
        auto future = task->get_future();
        asio::post(io_, [task] { (*task)(); });
        if (future.wait_for(kCommandTimeout) != std::future_status::ready) {
            return if_stopped;
        }
        return future.get();
    }

    // --- MeshTransport (I/O thread) ---

    void dial(mesh::ConnectionId id, const mesh::PeerAddress& address) override {
        const ConnectionPtr connection = add(id);
        start_connect_timer(connection);
        connection->resolver.async_resolve(
            address.host, std::to_string(address.port),
            [this, connection](const asio::error_code& error,
                               const tcp::resolver::results_type& results) {
                if (connection->closing || connection->connect_done) {
                    return;
                }
                if (error) {
                    connect_finished(connection, error);
                    return;
                }
                asio::async_connect(connection->socket, results,
                                    [this, connection](const asio::error_code& connect_error,
                                                       const tcp::endpoint& /*endpoint*/) {
                                        connect_finished(connection, connect_error);
                                    });
            });
    }

    void dial_peer_of(mesh::ConnectionId id, mesh::ConnectionId via, std::uint16_t port) override {
        const ConnectionPtr connection = add(id);
        asio::error_code error;
        const ConnectionPtr control = find(via);
        const tcp::endpoint remote =
            control ? control->socket.remote_endpoint(error) : tcp::endpoint{};
        if (!control || error) {
            // Never back into the mesh from inside its own call.
            asio::post(io_, [this, connection] {
                connect_finished(connection, asio::error::not_connected);
            });
            return;
        }
        start_connect_timer(connection);
        connection->socket.async_connect(tcp::endpoint(remote.address(), port),
                                         [this, connection](const asio::error_code& connect_error) {
                                             connect_finished(connection, connect_error);
                                         });
    }

    void send(mesh::ConnectionId id, std::span<const std::uint8_t> bytes) override {
        const ConnectionPtr connection = find(id);
        if (!connection || connection->closing) {
            return;
        }
        connection->queue.emplace_back(bytes.begin(), bytes.end());
        connection->queued += bytes.size();
        write_next(connection);
    }

    void send_datagram(mesh::ConnectionId id, std::span<const std::uint8_t> datagram) override {
        const ConnectionPtr connection = find(id);
        if (!connection || !connection->input.is_open() || !connection->input_peer) {
            return;
        }
        asio::error_code ignored; // lost like any datagram; the next position replaces it
        connection->input.send_to(asio::buffer(datagram.data(), datagram.size()),
                                  *connection->input_peer, 0, ignored);
    }

    [[nodiscard]] std::uint16_t input_port(mesh::ConnectionId id) override {
        const ConnectionPtr connection = find(id);
        if (!connection) {
            return 0;
        }
        asio::error_code error;
        if (!connection->input.is_open()) {
            const tcp::endpoint local = connection->socket.local_endpoint(error);
            if (error ||
                !open_input(*connection, local.protocol() == tcp::v6() ? udp::v6() : udp::v4())) {
                return 0;
            }
            mark_as_signaling(connection->socket); // only sessions have input sockets
            receive_datagram(connection);
        }
        const std::uint16_t port = connection->input.local_endpoint(error).port();
        return error ? std::uint16_t{0} : port;
    }

    void set_input_destination(mesh::ConnectionId id, std::uint16_t port) override {
        const ConnectionPtr connection = find(id);
        if (!connection) {
            return;
        }
        asio::error_code error;
        const tcp::endpoint remote = connection->socket.remote_endpoint(error);
        if (!error) {
            connection->input_peer = udp::endpoint(remote.address(), port);
        }
    }

    void accept_input_source(mesh::ConnectionId id) override {
        if (const ConnectionPtr connection = find(id)) {
            connection->input_peer = connection->datagram_source;
        }
    }

    [[nodiscard]] std::size_t queued_bytes(mesh::ConnectionId id) const override {
        const auto it = connections_.find(id);
        return it == connections_.end() ? 0 : it->second->queued;
    }

    void close(mesh::ConnectionId id) override {
        const ConnectionPtr connection = find(id);
        if (!connection) {
            return;
        }
        connections_.erase(id);
        connection->closing = true;
        asio::error_code ignored;
        connection->connect_timer.cancel();
        connection->resolver.cancel();
        connection->input.close(ignored);
        if (!connection->writing) {
            // Bytes still queued go out first; a dial in progress is aborted.
            if (connection->queue.empty() || !connection->connect_done) {
                close_sockets(*connection);
            } else {
                write_next(connection);
            }
        }
    }

    [[nodiscard]] std::uint16_t listen_port() const override { return listen_port_; }

private:
    ConnectionPtr add(mesh::ConnectionId id) {
        auto connection = std::make_shared<Connection>(io_, id);
        connections_[id] = connection;
        return connection;
    }

    [[nodiscard]] ConnectionPtr find(mesh::ConnectionId id) const {
        const auto it = connections_.find(id);
        return it == connections_.end() ? nullptr : it->second;
    }

    bool open_acceptor(std::uint16_t port, asio::error_code& error) {
        for (const auto protocol : {tcp::v6(), tcp::v4()}) {
            error.clear();
            asio::error_code ignored;
            acceptor_.close(ignored);
            acceptor_.open(protocol, error);
            if (error) {
                continue;
            }
            if (protocol == tcp::v6()) {
                acceptor_.set_option(asio::ip::v6_only(false), ignored); // IPv4 as well
            }
#ifndef _WIN32
            acceptor_.set_option(tcp::acceptor::reuse_address(true), ignored);
#endif
            acceptor_.bind(tcp::endpoint(protocol, port), error);
            if (!error) {
                acceptor_.listen(kListenBacklog, error);
            }
            if (!error) {
                return true;
            }
        }
        asio::error_code ignored;
        acceptor_.close(ignored);
        return false;
    }

    static bool open_input(Connection& connection, const udp& protocol) {
        asio::error_code error;
        connection.input.open(protocol, error);
        if (error) {
            return false;
        }
        asio::error_code ignored;
        if (protocol == udp::v6()) {
            connection.input.set_option(asio::ip::v6_only(false), ignored);
        }
        connection.input.bind(udp::endpoint(protocol, 0), error);
        if (error) {
            connection.input.close(ignored);
            return false;
        }
        connection.input.non_blocking(true, ignored);
        mark_as_interactive_voice(connection.input); // best effort: unmarked still works
        return true;
    }

    void accept_next() {
        auto socket = std::make_shared<tcp::socket>(io_);
        acceptor_.async_accept(*socket, [this, socket](const asio::error_code& error) {
            if (error) {
                if (error != asio::error::operation_aborted && acceptor_.is_open()) {
                    accept_next();
                }
                return;
            }
            const auto id = mesh_.on_accepted(now());
            if (!id) {
                asio::error_code ignored;
                socket->close(ignored);
            } else {
                const ConnectionPtr connection = add(*id);
                connection->socket = std::move(*socket);
                connection->connect_done = true;
                asio::error_code ignored;
                connection->socket.set_option(tcp::no_delay(true), ignored);
                read_next(connection);
            }
            reschedule();
            accept_next();
        });
    }

    void start_connect_timer(const ConnectionPtr& connection) {
        connection->connect_timer.expires_after(kConnectTimeout);
        connection->connect_timer.async_wait([this, connection](const asio::error_code& error) {
            if (error || connection->closing || connection->connect_done) {
                return; // cancelled, or connected meanwhile
            }
            connection->resolver.cancel();
            asio::error_code ignored;
            connection->socket.close(ignored); // aborts the connect
            connect_finished(connection, asio::error::timed_out);
        });
    }

    void connect_finished(const ConnectionPtr& connection, const asio::error_code& error) {
        if (connection->closing || connection->connect_done) {
            return;
        }
        connection->connect_done = true;
        connection->connect_timer.cancel();
        if (error) {
            peer_failed(connection,
                        error == asio::error::timed_out ? Error::TimedOut : Error::NetworkError);
            return;
        }
        asio::error_code ignored;
        connection->socket.set_option(tcp::no_delay(true), ignored);
        read_next(connection);
        mesh_.on_connected(connection->id, now());
        write_next(connection); // anything queued while connecting
        reschedule();
    }

    void read_next(const ConnectionPtr& connection) {
        connection->socket.async_read_some(
            asio::buffer(connection->read_buffer),
            [this, connection](const asio::error_code& error, std::size_t size) {
                if (connection->closing) {
                    return;
                }
                if (error) {
                    peer_failed(connection, error == asio::error::eof ? Error::ConnectionClosed
                                                                      : Error::NetworkError);
                    return;
                }
                mesh_.on_bytes(connection->id, std::span(connection->read_buffer).first(size),
                               now());
                reschedule();
                if (!connection->closing) {
                    read_next(connection);
                }
            });
    }

    void receive_datagram(const ConnectionPtr& connection) {
        connection->input.async_receive_from(
            asio::buffer(connection->datagram_buffer), connection->datagram_source,
            [this, connection](const asio::error_code& error, std::size_t size) {
                if (connection->closing || error == asio::error::operation_aborted) {
                    return;
                }
                if (!error && size <= protocol::kMaxDatagramSize) {
                    mesh_.on_datagram(connection->id,
                                      std::span(connection->datagram_buffer).first(size), now());
                    reschedule();
                }
                if (!connection->closing && connection->input.is_open()) {
                    receive_datagram(connection);
                }
            });
    }

    void write_next(const ConnectionPtr& connection) {
        if (connection->writing || !connection->connect_done) {
            return;
        }
        if (connection->queue.empty()) {
            if (connection->closing) {
                close_sockets(*connection);
            }
            return;
        }
        connection->writing = true;
        const auto& front = connection->queue.front();
        connection->socket.async_write_some(
            asio::buffer(front.data() + connection->write_offset,
                         front.size() - connection->write_offset),
            [this, connection](const asio::error_code& error, std::size_t size) {
                connection->writing = false;
                if (error) {
                    connection->queue.clear();
                    connection->queued = 0;
                    if (connection->closing) {
                        close_sockets(*connection);
                    } else {
                        peer_failed(connection, Error::NetworkError);
                    }
                    return;
                }
                connection->write_offset += size;
                if (connection->write_offset == connection->queue.front().size()) {
                    connection->queued -= connection->queue.front().size();
                    connection->queue.pop_front();
                    connection->write_offset = 0;
                    if (!connection->closing) {
                        mesh_.on_sent(connection->id);
                        reschedule();
                    }
                }
                write_next(connection);
            });
    }

    /// The peer closed, a socket failed or a dial did not connect.
    void peer_failed(const ConnectionPtr& connection, Error reason) {
        if (connection->closing) {
            return;
        }
        connection->closing = true;
        connections_.erase(connection->id);
        connection->queue.clear();
        connection->queued = 0;
        close_sockets(*connection);
        mesh_.on_closed(connection->id, reason, now());
        reschedule();
    }

    static void close_sockets(Connection& connection) {
        asio::error_code ignored;
        connection.connect_timer.cancel();
        connection.resolver.cancel();
        connection.socket.shutdown(tcp::socket::shutdown_both, ignored);
        connection.socket.close(ignored);
        connection.input.close(ignored);
    }

    void reschedule() {
        if (stopping_) {
            return;
        }
        timer_.expires_at(std::min(mesh_.next_wakeup(), now() + kMaxTimerWait));
        timer_.async_wait([this](const asio::error_code& error) {
            if (error || stopping_) {
                return;
            }
            mesh_.on_timer(now());
            reschedule();
        });
    }

    void stop() {
        if (!thread_.joinable()) {
            return;
        }
        asio::post(io_, [this] {
            mesh_.close_all(protocol::ByeReason::ApplicationQuit, now());
            stopping_ = true;
            asio::error_code ignored;
            timer_.cancel();
            acceptor_.close(ignored);
        });
        work_.reset();
        if (finished_.get_future().wait_for(kShutdownGrace) != std::future_status::ready) {
            io_.stop(); // a peer that does not read its last bytes must not hold up quitting
        }
        thread_.join();
    }

    asio::io_context io_;
    asio::executor_work_guard<asio::io_context::executor_type> work_;
    tcp::acceptor acceptor_;
    asio::steady_timer timer_;
    std::map<mesh::ConnectionId, ConnectionPtr> connections_;
    std::atomic<std::uint16_t> listen_port_{0};
    bool stopping_ = false; // I/O thread only
    std::promise<void> finished_;
    std::thread thread_;
    mesh::Mesh mesh_; // declared last: destroyed first, before the sockets it refers to by ID
};

// --- AsioNode ------------------------------------------------------------------------------------

AsioNode::AsioNode(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

AsioNode::~AsioNode() = default;

Expected<std::unique_ptr<AsioNode>, Error>
AsioNode::create(mesh::MeshConfig config, crypto::KeyPair identity, crypto::RandomSource& random,
                 mesh::MeshObserver& observer, NodeOptions options) {
    using Result = Expected<std::unique_ptr<AsioNode>, Error>;
    auto impl = std::make_unique<Impl>(std::move(config), std::move(identity), random, observer);
    if (const auto listening = impl->listen(options.listen_port); !listening) {
        return Result::failure(listening.error());
    }
    impl->start();
    return std::unique_ptr<AsioNode>(
        new AsioNode(std::move(impl))); // NOLINT(cppcoreguidelines-owning-memory): private ctor
}

std::uint16_t AsioNode::listen_port() const {
    return impl_->port();
}

mesh::DeviceId AsioNode::device_id() const {
    return impl_->device_id();
}

void AsioNode::peer_discovered(const mesh::DeviceId& device, const protocol::Id& group,
                               const mesh::PeerAddress& address) {
    // Init-captures copy into non-const members, so moving the closure never copies a string.
    impl_->post([device, group, address = address](mesh::Mesh& mesh, TimePoint now) {
        mesh.peer_discovered(device, group, address, now);
    });
}

void AsioNode::peer_lost(const mesh::DeviceId& device) {
    impl_->post([device](mesh::Mesh& mesh, TimePoint now) { mesh.peer_lost(device, now); });
}

void AsioNode::set_group(const protocol::Id& group) {
    impl_->post([group](mesh::Mesh& mesh, TimePoint now) { mesh.set_group(group, now); });
}

void AsioNode::send_motion(const mesh::DeviceId& device, const protocol::PointerMotion& motion) {
    impl_->post([device, motion](mesh::Mesh& mesh, TimePoint now) {
        static_cast<void>(mesh.send_motion(device, motion, now)); // a lost position is replaced
    });
}

void AsioNode::close_transfer(const mesh::DeviceId& device, std::uint32_t stream_id) {
    impl_->post([device, stream_id](mesh::Mesh& mesh, TimePoint now) {
        mesh.close_transfer(device, stream_id, now);
    });
}

void AsioNode::disconnect(const mesh::DeviceId& device, protocol::ByeReason reason) {
    impl_->post([device, reason](mesh::Mesh& mesh, TimePoint now) {
        mesh.disconnect(device, reason, now);
    });
}

void AsioNode::close_all(protocol::ByeReason reason) {
    impl_->post([reason](mesh::Mesh& mesh, TimePoint now) { mesh.close_all(reason, now); });
}

Expected<mesh::ConnectionId, Error> AsioNode::pair_with(const mesh::PeerAddress& address) {
    using Result = Expected<mesh::ConnectionId, Error>;
    return impl_->run_sync<Result>(
        [address = address](mesh::Mesh& mesh, TimePoint now) {
            return Result(mesh.pair_with(address, now));
        },
        Result::failure(Error::Internal));
}

Status<Error> AsioNode::confirm_pairing(mesh::ConnectionId connection, bool accepted) {
    return impl_->run_sync<Status<Error>>(
        [connection, accepted](mesh::Mesh& mesh, TimePoint now) {
            return mesh.confirm_pairing(connection, accepted, now);
        },
        Status<Error>::failure(Error::Internal));
}

Status<Error> AsioNode::send_message(const mesh::DeviceId& device,
                                     const protocol::ControlMessage& message) {
    return impl_->run_sync<Status<Error>>(
        [device, message = message](mesh::Mesh& mesh, TimePoint now) {
            return mesh.send_message(device, message, now);
        },
        Status<Error>::failure(Error::Internal));
}

Expected<std::uint32_t, Error> AsioNode::open_transfer(const mesh::DeviceId& device) {
    using Result = Expected<std::uint32_t, Error>;
    return impl_->run_sync<Result>(
        [device](mesh::Mesh& mesh, TimePoint now) { return mesh.open_transfer(device, now); },
        Result::failure(Error::Internal));
}

Status<Error> AsioNode::send_transfer(const mesh::DeviceId& device, std::uint32_t stream_id,
                                      const protocol::TransferMessage& message) {
    return impl_->run_sync<Status<Error>>(
        [device, stream_id, message = message](mesh::Mesh& mesh, TimePoint now) {
            return mesh.send_transfer(device, stream_id, message, now);
        },
        Status<Error>::failure(Error::Internal));
}

} // namespace osdrop::transport
