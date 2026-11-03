// AsioNode on real loopback sockets: two nodes connect, motion travels over UDP, a saturated
// transfer stream does not hold up control messages, and nothing readable crosses the control
// connection (seen through a recording relay).
#include "crypto_test_support.hpp"
#include "transport/asio_node.hpp"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define OSDROP_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define OSDROP_SANITIZED 1
#endif
#endif
#ifndef OSDROP_SANITIZED
#define OSDROP_SANITIZED 0
#endif

namespace {

namespace mesh = osdrop::mesh;
namespace protocol = osdrop::protocol;
using osdrop::Error;
using osdrop::transport::AsioNode;
using std::chrono::milliseconds;
using SteadyClock = std::chrono::steady_clock;

constexpr protocol::Id kGroup{0x47, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
const std::string kMarker = "OSDROP-PLAINTEXT-MARKER";

/// Records callbacks from the I/O thread; tests wait on it from theirs.
class ThreadSafeObserver final : public mesh::MeshObserver {
public:
    bool wait_for(const std::function<bool()>& condition,
                  milliseconds timeout = milliseconds(5'000)) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, condition);
    }

    std::mutex& mutex() { return mutex_; }

    std::size_t connected = 0;
    std::vector<protocol::PointerMotion> motions;
    std::vector<SteadyClock::time_point> message_times;
    std::vector<std::uint32_t> opened;
    std::uint64_t transfer_bytes = 0;
    bool transfer_intact = true;
    std::uint64_t next_offset = 0;
    std::size_t writable = 0;

    mesh::TrustDecision on_trust_query(const osdrop::crypto::Key& /*key*/,
                                       const protocol::Hello& /*hello*/) override {
        return mesh::TrustDecision::Trusted;
    }
    void on_pairing_code(mesh::ConnectionId /*connection*/, std::uint32_t /*code*/,
                         const osdrop::crypto::Key& /*key*/,
                         const protocol::Hello& /*hello*/) override {}
    void on_paired(const osdrop::crypto::Key& /*key*/, const protocol::Hello& /*hello*/) override {}
    void on_pairing_failed(mesh::ConnectionId /*connection*/, Error /*reason*/) override {}
    void on_peer_connected(const mesh::DeviceId& /*device*/, const osdrop::crypto::Key& /*key*/,
                           const protocol::Hello& /*hello*/,
                           const protocol::Capabilities& /*capabilities*/) override {
        update([&] { ++connected; });
    }
    void on_peer_disconnected(const mesh::DeviceId& /*device*/,
                              std::optional<Error> /*reason*/) override {
        update([&] { --connected; });
    }
    void on_peer_responsive(const mesh::DeviceId& /*device*/, bool /*responsive*/) override {}
    void on_message(const mesh::DeviceId& /*device*/,
                    const protocol::ControlMessage& /*message*/) override {
        const auto at = SteadyClock::now();
        update([&] { message_times.push_back(at); });
    }
    void on_motion(const mesh::DeviceId& /*device*/,
                   const protocol::PointerMotion& motion) override {
        update([&] { motions.push_back(motion); });
    }
    void on_transfer_opened(const mesh::DeviceId& /*device*/, std::uint32_t stream_id) override {
        update([&] { opened.push_back(stream_id); });
    }
    void on_transfer_message(const mesh::DeviceId& /*device*/, std::uint32_t /*stream_id*/,
                             const protocol::TransferMessage& message) override {
        const auto* chunk = std::get_if<protocol::ContentChunk>(&message);
        update([&] {
            if (chunk == nullptr || chunk->offset != next_offset) {
                transfer_intact = false;
                return;
            }
            transfer_bytes += chunk->data.size();
            next_offset += chunk->data.size();
        });
    }
    void on_transfer_writable(const mesh::DeviceId& /*device*/,
                              std::uint32_t /*stream_id*/) override {
        update([&] { ++writable; });
    }
    void on_transfer_closed(const mesh::DeviceId& /*device*/, std::uint32_t /*stream_id*/,
                            std::optional<Error> /*reason*/) override {}
    void on_stats(const mesh::DeviceId& /*device*/,
                  const osdrop::session::SessionStats& /*stats*/) override {}
    void on_log(mesh::LogLevel /*level*/, std::string_view /*message*/) override {}

private:
    template <typename Change>
    void update(Change&& change) {
        {
            std::lock_guard lock(mutex_);
            change();
        }
        changed_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable changed_;
};

struct Node {
    explicit Node(std::uint64_t seed, const std::string& name) : random(seed) {
        auto config = mesh::MeshConfig{};
        config.device_name = name;
        config.group_id = kGroup;
        config.capabilities =
            protocol::Capabilities{.features = protocol::kKnownFeatures, .max_transfer_streams = 4};
        auto created = AsioNode::create(config, osdrop::test::key_pair(random), random, observer);
        if (!created) {
            throw std::runtime_error("node did not start");
        }
        node = std::move(created.value());
    }

    osdrop::test::SeededRandom random;
    ThreadSafeObserver observer;
    std::unique_ptr<AsioNode> node;
};

/// Makes `from` find `to` at 127.0.0.1 on `port` (its own listener unless a relay sits between).
void discover(Node& from, Node& to, std::uint16_t port) {
    from.node->peer_discovered(to.node->device_id(), kGroup, {"127.0.0.1", port});
}

void connect(Node& a, Node& b) {
    discover(a, b, b.node->listen_port());
    discover(b, a, a.node->listen_port());
    ASSERT_TRUE(a.observer.wait_for([&] { return a.observer.connected == 1; }));
    ASSERT_TRUE(b.observer.wait_for([&] { return b.observer.connected == 1; }));
}

TEST(AsioNode, TwoNodesConnectAndSendMotionOverUdp) {
    Node a(1, "A");
    Node b(2, "B");
    EXPECT_NE(a.node->listen_port(), 0);
    connect(a, b);
    for (std::int32_t x = 1; x <= 20; ++x) {
        a.node->send_motion(
            b.node->device_id(),
            protocol::PointerMotion{.control_id = 1, .x = x, .y = 2, .capture_time_us = 3});
        std::this_thread::sleep_for(milliseconds(1));
    }
    ASSERT_TRUE(b.observer.wait_for(
        [&] { return !b.observer.motions.empty() && b.observer.motions.back().x == 20; }));
}

TEST(AsioNode, ATransferStreamCarriesDataInOrder) {
    Node a(3, "A");
    Node b(4, "B");
    connect(a, b);
    const auto stream = a.node->open_transfer(b.node->device_id());
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(a.observer.wait_for([&] { return a.observer.writable >= 1; }));

    constexpr std::size_t kChunk = protocol::kMaxChunkData;
    constexpr std::uint64_t kTotal = std::uint64_t{32} * 1024 * 1024;
    std::vector<std::uint8_t> data(kChunk, 0x5A);
    const auto started = SteadyClock::now();
    for (std::uint64_t offset = 0; offset < kTotal;) {
        const auto sent = a.node->send_transfer(
            b.node->device_id(), stream.value(),
            protocol::ContentChunk{.request_id = 1, .offset = offset, .data = data});
        if (!sent && sent.error() == Error::PeerBusy) {
            const std::size_t seen = [&] {
                std::lock_guard lock(a.observer.mutex());
                return a.observer.writable;
            }();
            a.observer.wait_for([&] { return a.observer.writable > seen; }, milliseconds(1'000));
            continue;
        }
        ASSERT_TRUE(sent.has_value());
        offset += kChunk;
    }
    ASSERT_TRUE(b.observer.wait_for([&] { return b.observer.transfer_bytes >= kTotal; },
                                    milliseconds(20'000)));
    const auto elapsed = std::chrono::duration<double>(SteadyClock::now() - started).count();
    std::lock_guard lock(b.observer.mutex());
    EXPECT_TRUE(b.observer.transfer_intact);
    RecordProperty("loopback_megabytes_per_second",
                   std::to_string(static_cast<double>(kTotal) / 1e6 / elapsed));
}

TEST(AsioNode, ControlMessagesStayFastWhileATransferStreamIsSaturated) {
    Node a(5, "A");
    Node b(6, "B");
    connect(a, b);
    const auto stream = a.node->open_transfer(b.node->device_id());
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(a.observer.wait_for([&] { return a.observer.writable >= 1; }));

    // Keep the stream busy from another thread for the whole measurement. (Whether a full queue
    // holds up control is checked deterministically in the mesh simulation.)
    std::atomic<bool> stop{false};
    std::thread saturate([&] {
        std::vector<std::uint8_t> data(protocol::kMaxChunkData, 0x33);
        std::uint64_t offset = 0;
        while (!stop.load()) {
            const auto sent = a.node->send_transfer(
                b.node->device_id(), stream.value(),
                protocol::ContentChunk{.request_id = 1, .offset = offset, .data = data});
            if (sent) {
                offset += data.size();
            } else {
                std::this_thread::sleep_for(milliseconds(1));
            }
        }
    });
    const auto received_bytes = [&] {
        std::lock_guard lock(b.observer.mutex());
        return b.observer.transfer_bytes;
    };
    ASSERT_TRUE(b.observer.wait_for([&] { return b.observer.transfer_bytes >= 1024 * 1024; },
                                    milliseconds(20'000)))
        << "the stream never got going";
    const std::uint64_t bytes_before = received_bytes();
    std::vector<double> latencies_ms;
    for (std::uint32_t i = 1; i <= 50; ++i) {
        const auto sent_at = SteadyClock::now();
        ASSERT_TRUE(a.node
                        ->send_message(b.node->device_id(),
                                       protocol::DragResult{
                                           .drag_id = i, .outcome = protocol::DragOutcome::Dropped})
                        .has_value());
        ASSERT_TRUE(b.observer.wait_for([&] { return b.observer.message_times.size() == i; },
                                        milliseconds(20'000)));
        std::lock_guard lock(b.observer.mutex());
        latencies_ms.push_back(
            std::chrono::duration<double, std::milli>(b.observer.message_times.back() - sent_at)
                .count());
    }
    const std::uint64_t moved = received_bytes() - bytes_before;
    stop = true;
    saturate.join();
    EXPECT_GT(moved, std::uint64_t{protocol::kMaxChunkData}) << "the stream kept moving";

    std::ranges::sort(latencies_ms);
    const double p90 = latencies_ms.at(latencies_ms.size() * 9 / 10);
    RecordProperty("control_latency_p90_ms", std::to_string(p90));
#if !OSDROP_SANITIZED
    // Instrumented builds run ten times slower; the absolute budget holds for real builds.
#ifdef NDEBUG
    constexpr double kBudgetMs = 5.0;
#else
    constexpr double kBudgetMs = 20.0;
#endif
    EXPECT_LT(p90, kBudgetMs) << "median " << latencies_ms.at(latencies_ms.size() / 2) << " ms";
#endif
}

/// Forwards one TCP connection to `target` on its own thread and records both directions.
class RecordingRelay {
public:
    explicit RecordingRelay(std::uint16_t target)
        : acceptor_(io_, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0)),
          client_(io_), server_(io_), target_(target) {
        acceptor_.async_accept(client_, [this](const asio::error_code& error) {
            if (error) {
                return;
            }
            server_.async_connect(
                asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), target_),
                [this](const asio::error_code& connect_error) {
                    if (!connect_error) {
                        pump(client_, server_, client_buffer_);
                        pump(server_, client_, server_buffer_);
                    }
                });
        });
        thread_ = std::thread([this] { io_.run(); });
    }
    ~RecordingRelay() {
        io_.stop();
        thread_.join();
    }
    RecordingRelay(const RecordingRelay&) = delete;
    RecordingRelay& operator=(const RecordingRelay&) = delete;
    RecordingRelay(RecordingRelay&&) = delete;
    RecordingRelay& operator=(RecordingRelay&&) = delete;

    [[nodiscard]] std::uint16_t port() const { return acceptor_.local_endpoint().port(); }
    [[nodiscard]] std::vector<std::uint8_t> recorded() {
        std::lock_guard lock(mutex_);
        return recorded_;
    }

private:
    using Buffer = std::array<std::uint8_t, 16'384>;

    void pump(asio::ip::tcp::socket& from, asio::ip::tcp::socket& to, Buffer& buffer) {
        from.async_read_some(
            asio::buffer(buffer),
            [this, &from, &to, &buffer](const asio::error_code& error, std::size_t size) {
                if (error) {
                    asio::error_code ignored;
                    to.shutdown(asio::ip::tcp::socket::shutdown_send, ignored);
                    return;
                }
                {
                    std::lock_guard lock(mutex_);
                    recorded_.insert(recorded_.end(), buffer.begin(),
                                     buffer.begin() + static_cast<std::ptrdiff_t>(size));
                }
                asio::async_write(to, asio::buffer(buffer.data(), size),
                                  [this, &from, &to, &buffer](const asio::error_code& write_error,
                                                              std::size_t /*written*/) {
                                      if (!write_error) {
                                          pump(from, to, buffer);
                                      }
                                  });
            });
    }

    asio::io_context io_;
    asio::ip::tcp::acceptor acceptor_;
    asio::ip::tcp::socket client_;
    asio::ip::tcp::socket server_;
    Buffer client_buffer_{};
    Buffer server_buffer_{};
    std::uint16_t target_;
    std::mutex mutex_;
    std::vector<std::uint8_t> recorded_;
    std::thread thread_;
};

TEST(AsioNode, NothingReadableCrossesTheControlConnection) {
    Node a(7, "A");
    Node b(8, "B");
    // The lower device ID dials; route its dial through the relay.
    Node& dialler = a.node->device_id() < b.node->device_id() ? a : b;
    Node& listener = &dialler == &a ? b : a;
    RecordingRelay relay(listener.node->listen_port());
    discover(dialler, listener, relay.port());
    discover(listener, dialler, dialler.node->listen_port());
    ASSERT_TRUE(dialler.observer.wait_for([&] { return dialler.observer.connected == 1; }));

    const protocol::DragOffer offer{.probe_id = 1,
                                    .drag_id = 2,
                                    .items = {{.kind = protocol::ItemKind::File,
                                               .byte_count = 1,
                                               .content_type = "public.data",
                                               .name = kMarker + ".txt"}}};
    ASSERT_TRUE(dialler.node->send_message(listener.node->device_id(), offer).has_value());
    ASSERT_TRUE(
        listener.observer.wait_for([&] { return listener.observer.message_times.size() == 1; }));

    const auto recorded = relay.recorded();
    EXPECT_GT(recorded.size(), 200U);
    EXPECT_EQ(std::search(recorded.begin(), recorded.end(), kMarker.begin(), kMarker.end()),
              recorded.end());
}

} // namespace
