#pragma once

// A deterministic network of meshes for tests: a simulated clock, TCP connections that are
// reliable and ordered (delayed by `latency`, optionally limited in bandwidth each), input
// datagrams that can be lost and reordered, nodes that can freeze (power loss), and a capture of
// every byte on the wire.

#include "crypto_test_support.hpp"
#include "mesh/mesh.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace osdrop::test {

using mesh::ConnectionId;
using mesh::DeviceId;
using std::chrono::milliseconds;

class SimulatedMeshNetwork;

/// Records everything a mesh reports.
class RecordingMeshObserver final : public mesh::MeshObserver {
public:
    mesh::TrustDecision trust = mesh::TrustDecision::Trusted;
    std::set<DeviceId> connected;
    std::vector<std::pair<DeviceId, std::optional<Error>>> disconnects;
    std::vector<std::pair<DeviceId, bool>> responsiveness;
    std::vector<std::pair<ConnectionId, std::uint32_t>> pairing_codes;
    std::vector<crypto::Key> paired;
    std::vector<std::pair<ConnectionId, Error>> pairing_failures;
    std::vector<std::pair<DeviceId, protocol::ControlMessage>> messages;
    std::vector<std::pair<DeviceId, protocol::PointerMotion>> motions;
    std::vector<std::pair<DeviceId, std::uint32_t>> transfers_opened;
    std::vector<std::pair<std::uint32_t, protocol::TransferMessage>> transfer_messages;
    std::vector<std::uint32_t> writable;
    std::vector<std::pair<std::uint32_t, std::optional<Error>>> transfers_closed;
    std::size_t connects = 0;

    mesh::TrustDecision on_trust_query(const crypto::Key& /*key*/,
                                       const protocol::Hello& /*hello*/) override {
        return trust;
    }
    void on_pairing_code(ConnectionId connection, std::uint32_t code, const crypto::Key& /*key*/,
                         const protocol::Hello& /*hello*/) override {
        pairing_codes.emplace_back(connection, code);
    }
    void on_paired(const crypto::Key& key, const protocol::Hello& /*hello*/) override {
        paired.push_back(key);
    }
    void on_pairing_failed(ConnectionId connection, Error reason) override {
        pairing_failures.emplace_back(connection, reason);
    }
    void on_peer_connected(const DeviceId& device, const crypto::Key& /*key*/,
                           const protocol::Hello& /*hello*/,
                           const protocol::Capabilities& /*capabilities*/) override {
        connected.insert(device);
        ++connects;
    }
    void on_peer_disconnected(const DeviceId& device, std::optional<Error> reason) override {
        connected.erase(device);
        disconnects.emplace_back(device, reason);
    }
    void on_peer_responsive(const DeviceId& device, bool responsive) override {
        responsiveness.emplace_back(device, responsive);
    }
    void on_message(const DeviceId& device, const protocol::ControlMessage& message) override {
        messages.emplace_back(device, message);
    }
    void on_motion(const DeviceId& device, const protocol::PointerMotion& motion) override {
        motions.emplace_back(device, motion);
    }
    void on_transfer_opened(const DeviceId& device, std::uint32_t stream_id) override {
        transfers_opened.emplace_back(device, stream_id);
    }
    void on_transfer_message(const DeviceId& /*device*/, std::uint32_t stream_id,
                             const protocol::TransferMessage& message) override {
        transfer_messages.emplace_back(stream_id, message);
    }
    void on_transfer_writable(const DeviceId& /*device*/, std::uint32_t stream_id) override {
        writable.push_back(stream_id);
    }
    void on_transfer_closed(const DeviceId& /*device*/, std::uint32_t stream_id,
                            std::optional<Error> reason) override {
        transfers_closed.emplace_back(stream_id, reason);
    }
    void on_stats(const DeviceId& /*device*/, const session::SessionStats& /*stats*/) override {}
    void on_log(mesh::LogLevel /*level*/, std::string_view /*message*/) override {}
};

class SimulatedNode;

/// One end of a simulated TCP connection.
struct Endpoint {
    std::size_t node = 0;
    ConnectionId id = 0;
    bool operator<(const Endpoint& other) const {
        return std::pair(node, id) < std::pair(other.node, other.id);
    }
    bool operator==(const Endpoint&) const = default;
};

class SimulatedTransport final : public mesh::MeshTransport {
public:
    SimulatedTransport(SimulatedMeshNetwork& network, std::size_t node)
        : network_(&network), node_(node) {}

    void dial(ConnectionId id, const mesh::PeerAddress& address) override;
    void dial_peer_of(ConnectionId id, ConnectionId via, std::uint16_t port) override;
    void send(ConnectionId id, std::span<const std::uint8_t> bytes) override;
    void send_datagram(ConnectionId id, std::span<const std::uint8_t> datagram) override;
    std::uint16_t input_port(ConnectionId id) override {
        return static_cast<std::uint16_t>(40'000 + id);
    }
    void set_input_destination(ConnectionId /*id*/, std::uint16_t /*port*/) override {}
    void accept_input_source(ConnectionId /*id*/) override {}
    std::size_t queued_bytes(ConnectionId id) const override;
    void close(ConnectionId id) override;
    std::uint16_t listen_port() const override { return 49'152; }

private:
    SimulatedMeshNetwork* network_;
    std::size_t node_;
};

class SimulatedNode {
public:
    SimulatedNode(SimulatedMeshNetwork& network, std::size_t index, mesh::MeshConfig config,
                  std::uint64_t seed)
        : random(seed), transport(network, index),
          mesh(std::move(config), key_pair(random), random, observer, transport),
          address{"node" + std::to_string(index), 49'152} {}

    SeededRandom random;
    RecordingMeshObserver observer;
    SimulatedTransport transport;
    mesh::Mesh mesh;
    mesh::PeerAddress address;
    bool frozen = false; ///< power loss: processes and sends nothing
};

class SimulatedMeshNetwork {
public:
    Microseconds latency{500};
    double datagram_loss = 0.0;
    Microseconds datagram_jitter{0}; ///< extra random delay, which reorders datagrams
    /// When set, each direction of each TCP connection moves this many bytes per millisecond, and
    /// what has not been delivered yet counts as queued for writing.
    std::optional<double> bandwidth_bytes_per_ms;
    TimePoint now{std::chrono::seconds(1000)};
    std::vector<std::vector<std::uint8_t>> wire; ///< every TCP write and datagram, in order

    static mesh::MeshConfig config(const protocol::Id& group, std::string name) {
        mesh::MeshConfig config;
        config.device_name = std::move(name);
        config.group_id = group;
        config.capabilities =
            protocol::Capabilities{.features = protocol::kKnownFeatures, .max_transfer_streams = 4};
        return config;
    }

    SimulatedNode& add(const mesh::MeshConfig& node_config) {
        const std::size_t index = nodes_.size();
        nodes_.push_back(std::make_unique<SimulatedNode>(*this, index, node_config, 7 + index));
        return *nodes_.back();
    }
    SimulatedNode& node(std::size_t index) { return *nodes_.at(index); }
    [[nodiscard]] std::size_t size() const { return nodes_.size(); }

    /// Every node discovers every other one with its group.
    void discover_all(const protocol::Id& group) {
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            for (std::size_t j = 0; j < nodes_.size(); ++j) {
                if (i != j) {
                    node(i).mesh.peer_discovered(node(j).mesh.device_id(), group, node(j).address,
                                                 now);
                }
            }
        }
    }

    void run_for(Microseconds duration) {
        const TimePoint end = now + duration;
        while (now < end) {
            step();
        }
    }

    /// Advances the clock by one millisecond: delivers what is due, then runs the timers of the
    /// nodes whose next wakeup has come (which also checks next_wakeup).
    void step() {
        now += milliseconds(1);
        std::stable_sort(queue_.begin(), queue_.end(),
                         [](const auto& a, const auto& b) { return a.at < b.at; });
        std::vector<Event> held;
        while (!queue_.empty() && queue_.front().at <= now) {
            Event event = std::move(queue_.front());
            queue_.pop_front();
            if (holds(event)) {
                held.push_back(std::move(event)); // TCP keeps retrying until the node is back
            } else {
                deliver(event);
            }
        }
        for (Event& event : held) {
            event.at = now + milliseconds(1);
            queue_.push_back(std::move(event));
        }
        for (auto& simulated : nodes_) {
            if (!simulated->frozen && now >= simulated->mesh.next_wakeup()) {
                simulated->mesh.on_timer(now);
            }
        }
    }

    /// Breaks one TCP connection: both ends see a network error.
    void break_connection(Endpoint end) {
        const auto it = links_.find(end);
        if (it == links_.end()) {
            return;
        }
        const Endpoint other = it->second;
        links_.erase(end);
        links_.erase(other);
        node(end.node).mesh.on_closed(end.id, Error::NetworkError, now);
        node(other.node).mesh.on_closed(other.id, Error::NetworkError, now);
    }

    /// The open TCP connections, as (lower end, higher end) pairs.
    [[nodiscard]] std::set<std::pair<Endpoint, Endpoint>> open_links() const {
        std::set<std::pair<Endpoint, Endpoint>> links;
        for (const auto& [from, to] : links_) {
            links.insert(std::min(from, to) == from ? std::pair(from, to) : std::pair(to, from));
        }
        return links;
    }

    /// Opens a raw TCP connection to `target` from outside any mesh (a forged client). Returns
    /// the client's handle; bytes the node sends back are discarded.
    std::size_t raw_connect(std::size_t target) {
        const auto id = node(target).mesh.on_accepted(now);
        if (!id) {
            return 0;
        }
        const std::size_t handle = next_raw_++;
        raw_[handle] = Endpoint{.node = target, .id = *id};
        raw_open_[handle] = true;
        links_[Endpoint{.node = kRaw, .id = handle}] = raw_[handle];
        links_[raw_[handle]] = Endpoint{.node = kRaw, .id = handle};
        return handle;
    }
    void raw_send(std::size_t handle, std::span<const std::uint8_t> bytes) {
        node(raw_.at(handle).node).mesh.on_bytes(raw_.at(handle).id, bytes, now);
    }
    [[nodiscard]] bool raw_open(std::size_t handle) const { return raw_open_.at(handle); }

    // --- From the transports ---

    void dial(std::size_t from, ConnectionId id, std::size_t to) {
        schedule({.at = now + latency, .kind = Event::Kind::Accept, .from = {from, id}, .to = to});
    }

    /// Nothing listens there: the dial fails after a round trip (never inside the mesh's call).
    void dial_nowhere(std::size_t from, ConnectionId id) {
        schedule({.at = now + latency, .kind = Event::Kind::DialFailed, .from = {from, id}});
    }

    [[nodiscard]] std::optional<std::size_t> node_at(const std::string& host) const {
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (nodes_[i]->address.host == host) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::size_t> remote_node(std::size_t node_index,
                                                         ConnectionId id) const {
        const auto it = links_.find(Endpoint{.node = node_index, .id = id});
        return it == links_.end() ? std::nullopt : std::optional(it->second.node);
    }

    void send(std::size_t from, ConnectionId id, std::span<const std::uint8_t> bytes) {
        if (node(from).frozen) {
            return;
        }
        const auto it = links_.find(Endpoint{.node = from, .id = id});
        if (it == links_.end()) {
            return;
        }
        wire.emplace_back(bytes.begin(), bytes.end());
        Event event{.kind = Event::Kind::Bytes,
                    .from = {from, id},
                    .target = it->second,
                    .bytes = {bytes.begin(), bytes.end()}};
        if (bandwidth_bytes_per_ms) {
            // Bytes leave one after another at the link's rate; until then they are queued.
            TimePoint& free = link_free_[event.from];
            const auto transmit = Microseconds{static_cast<std::int64_t>(
                static_cast<double>(bytes.size()) * 1000.0 / *bandwidth_bytes_per_ms)};
            free = std::max(free, now) + transmit;
            event.at = free;
            event.queued_size = bytes.size();
            queued_[event.from] += bytes.size();
        }
        enqueue_ordered(std::move(event));
    }

    [[nodiscard]] std::size_t queued(std::size_t node_index, ConnectionId id) const {
        const auto it = queued_.find(Endpoint{.node = node_index, .id = id});
        return it == queued_.end() ? 0 : it->second;
    }

    void send_datagram(std::size_t from, ConnectionId id, std::span<const std::uint8_t> bytes) {
        if (node(from).frozen) {
            return;
        }
        const auto it = links_.find(Endpoint{.node = from, .id = id});
        if (it == links_.end()) {
            return;
        }
        wire.emplace_back(bytes.begin(), bytes.end());
        if (draw() < datagram_loss) {
            return;
        }
        const auto jitter = Microseconds{
            static_cast<std::int64_t>(draw() * static_cast<double>(datagram_jitter.count()))};
        schedule({.at = now + latency + jitter,
                  .kind = Event::Kind::Datagram,
                  .from = {from, id},
                  .target = it->second,
                  .bytes = {bytes.begin(), bytes.end()}});
    }

    void close(std::size_t from, ConnectionId id) {
        const auto it = links_.find(Endpoint{.node = from, .id = id});
        if (it == links_.end()) {
            return;
        }
        const Endpoint target = it->second;
        links_.erase(it);
        enqueue_ordered({.kind = Event::Kind::Close, .from = {from, id}, .target = target});
    }

private:
    static constexpr std::size_t kRaw = static_cast<std::size_t>(-1);

    struct Event {
        enum class Kind : std::uint8_t { Accept, DialFailed, Bytes, Datagram, Close };
        TimePoint at;
        Kind kind = Kind::Bytes;
        Endpoint from;
        Endpoint target;
        std::size_t to = 0; ///< Accept: the node dialled
        std::vector<std::uint8_t> bytes;
        std::size_t queued_size = 0; ///< counted in queued_ until delivered
    };

    double draw() {
        rng_ = rng_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(rng_ >> 11U) / static_cast<double>(1ULL << 53U);
    }

    void schedule(Event event) { queue_.push_back(std::move(event)); }

    /// TCP data and closes for a frozen node wait; datagrams for it are lost.
    [[nodiscard]] bool holds(const Event& event) const {
        return (event.kind == Event::Kind::Bytes || event.kind == Event::Kind::Close) &&
               event.target.node != kRaw && nodes_.at(event.target.node)->frozen;
    }

    /// TCP: in order per direction of a connection.
    void enqueue_ordered(Event event) {
        TimePoint at = std::max(event.at, now) + latency;
        TimePoint& last = last_ordered_[event.from];
        at = std::max(at, last);
        last = at;
        event.at = at;
        queue_.push_back(std::move(event));
    }

    void deliver(const Event& event) {
        if (event.kind == Event::Kind::DialFailed) {
            node(event.from.node).mesh.on_closed(event.from.id, Error::NetworkError, now);
            return;
        }
        if (event.kind == Event::Kind::Accept) {
            SimulatedNode& dialler = node(event.from.node);
            SimulatedNode& target = node(event.to);
            if (target.frozen) {
                dialler.mesh.on_closed(event.from.id, Error::NetworkError, now);
                return;
            }
            const auto accepted = target.mesh.on_accepted(now);
            if (!accepted) {
                dialler.mesh.on_closed(event.from.id, Error::NetworkError, now);
                return;
            }
            const Endpoint server{.node = event.to, .id = *accepted};
            links_[event.from] = server;
            links_[server] = event.from;
            dialler.mesh.on_connected(event.from.id, now);
            return;
        }
        if (event.target.node == kRaw) {
            if (event.kind == Event::Kind::Close) {
                raw_open_[event.target.id] = false;
            }
            return;
        }
        SimulatedNode& target = node(event.target.node);
        if (target.frozen) {
            return;
        }
        switch (event.kind) {
        case Event::Kind::Bytes:
            target.mesh.on_bytes(event.target.id, event.bytes, now);
            if (event.queued_size > 0) {
                queued_[event.from] -= event.queued_size;
                if (!node(event.from.node).frozen) {
                    node(event.from.node).mesh.on_sent(event.from.id);
                }
            }
            break;
        case Event::Kind::Datagram: {
            std::vector<std::uint8_t> datagram = event.bytes;
            target.mesh.on_datagram(event.target.id, datagram, now);
            break;
        }
        case Event::Kind::Close:
            if (links_.erase(event.target) > 0) {
                target.mesh.on_closed(event.target.id, Error::ConnectionClosed, now);
            }
            break;
        case Event::Kind::Accept:
        case Event::Kind::DialFailed:
            break;
        }
    }

    std::vector<std::unique_ptr<SimulatedNode>> nodes_;
    std::deque<Event> queue_;
    std::map<Endpoint, Endpoint> links_;
    std::map<Endpoint, TimePoint> last_ordered_;
    std::map<Endpoint, TimePoint> link_free_;
    std::map<Endpoint, std::size_t> queued_;
    std::map<std::size_t, Endpoint> raw_;
    std::map<std::size_t, bool> raw_open_;
    std::size_t next_raw_ = 1;
    std::uint64_t rng_ = 0x5EED;
};

inline void SimulatedTransport::dial(ConnectionId id, const mesh::PeerAddress& address) {
    const auto target = network_->node_at(address.host);
    if (!target) {
        network_->dial_nowhere(node_, id);
        return;
    }
    network_->dial(node_, id, *target);
}

inline void SimulatedTransport::dial_peer_of(ConnectionId id, ConnectionId via,
                                             std::uint16_t /*port*/) {
    const auto target = network_->remote_node(node_, via);
    if (!target) {
        network_->dial_nowhere(node_, id);
        return;
    }
    network_->dial(node_, id, *target);
}

inline void SimulatedTransport::send(ConnectionId id, std::span<const std::uint8_t> bytes) {
    network_->send(node_, id, bytes);
}

inline void SimulatedTransport::send_datagram(ConnectionId id,
                                              std::span<const std::uint8_t> datagram) {
    network_->send_datagram(node_, id, datagram);
}

inline void SimulatedTransport::close(ConnectionId id) {
    network_->close(node_, id);
}

inline std::size_t SimulatedTransport::queued_bytes(ConnectionId id) const {
    return network_->queued(node_, id);
}

} // namespace osdrop::test
