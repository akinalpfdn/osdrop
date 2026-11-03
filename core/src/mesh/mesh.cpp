#include "mesh/mesh.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <variant>

namespace osdrop::mesh {

namespace {

constexpr std::size_t kLoggedIdBytes = 4;

std::string short_id(const DeviceId& id) {
    constexpr std::string_view kHex = "0123456789abcdef";
    std::string text;
    for (std::size_t i = 0; i < kLoggedIdBytes; ++i) {
        text.push_back(kHex.at(id.at(i) >> 4U));
        text.push_back(kHex.at(id.at(i) & 0x0FU));
    }
    return text;
}

bool is_zero(const protocol::Id& id) {
    return std::ranges::all_of(id, [](std::uint8_t byte) { return byte == 0; });
}

/// Reasons after which a peer is not dialled again until it is discovered again.
bool blocks_redial(std::optional<Error> reason) {
    return reason == Error::NotTrusted || reason == Error::PairingRejected ||
           reason == Error::UnsupportedVersion;
}

} // namespace

struct Mesh::Peer {
    DeviceId id{};
    protocol::Id group{};
    std::optional<PeerAddress> address;
    bool discovered = false;
    TimePoint discovered_at;
    TimePoint next_dial;
    Microseconds backoff{0};
    bool refused = false; ///< refused this computer: no dial until trust changes or rediscovered
    bool held = false;    ///< disconnected on purpose: no dial until rediscovered
};

struct Mesh::Connection {
    enum class Kind : std::uint8_t { Pending, Control, Transfer };

    ConnectionId id = 0;
    Kind kind = Kind::Pending;
    bool outbound = false;
    bool connected = false;
    bool closed = false; ///< removed at the end of the entry point
    TimePoint opened_at;
    std::vector<std::uint8_t> pending; ///< accepted, before the first frame is complete

    // Control connections
    std::unique_ptr<ConnectionAdapter> adapter;
    std::unique_ptr<session::SessionEngine> engine;
    std::optional<DeviceId> expected; ///< dialled for this discovered peer
    DeviceId peer{}; ///< valid when peer_known: authenticated; transfer streams: their peer
    bool peer_known = false;
    bool pairing = false;
    bool duplicate = false;
    bool connected_reported = false;
    std::uint32_t next_stream_id = 0;
    std::set<std::uint32_t> streams; ///< transfer stream IDs used in this session

    // Transfer streams
    ConnectionId control = 0;
    std::unique_ptr<TransferChannel> channel;
    protocol::Id peer_token{}; ///< opened here: the token for the hello
    bool blocked = false;      ///< queue full; writable notification owed
};

class Mesh::ConnectionAdapter final : public session::SessionObserver,
                                      public session::SessionTransport {
public:
    ConnectionAdapter(Mesh& mesh, Connection& connection)
        : mesh_(&mesh), connection_(&connection) {}

    // SessionObserver
    void on_state(session::SessionState state, std::optional<Error> reason) override {
        mesh_->session_state(*connection_, state, reason, mesh_->now_);
    }
    void on_peer_hello(const protocol::Hello& /*hello*/) override {}
    TrustVerdict on_trust_query(const crypto::Key& peer_static_key) override {
        return mesh_->trust_query(*connection_, peer_static_key);
    }
    void on_pairing_code(std::uint32_t code) override {
        if (connection_->duplicate) {
            return; // closed in a moment; nothing to show
        }
        connection_->pairing = true;
        mesh_->observer_.on_pairing_code(connection_->id, code,
                                         connection_->engine->peer_static_key(),
                                         connection_->engine->peer_hello());
    }
    void on_paired() override {
        mesh_->observer_.on_paired(connection_->engine->peer_static_key(),
                                   connection_->engine->peer_hello());
    }
    void on_established(const protocol::Capabilities& peer) override {
        mesh_->session_established(*connection_, peer);
    }
    void on_capabilities(const protocol::Capabilities& peer) override {
        if (connection_->connected_reported) {
            mesh_->observer_.on_peer_capabilities(connection_->peer, peer);
        }
    }
    void on_responsive(bool responsive) override {
        if (connection_->connected_reported) {
            mesh_->observer_.on_peer_responsive(connection_->peer, responsive);
        }
    }
    void on_message(const protocol::ControlMessage& message) override {
        mesh_->observer_.on_message(connection_->peer, message);
    }
    void on_motion(const protocol::PointerMotion& motion) override {
        mesh_->observer_.on_motion(connection_->peer, motion);
    }
    void on_stats(const SessionStats& stats) override {
        if (connection_->connected_reported) {
            mesh_->observer_.on_stats(connection_->peer, stats);
        }
    }
    void on_log(LogLevel level, std::string_view message) override {
        std::string line = "[" + std::to_string(connection_->id);
        if (connection_->peer_known) {
            line += " " + short_id(connection_->peer);
        }
        line += "] ";
        line += message;
        mesh_->log(level, line);
    }

    // SessionTransport
    void send_control(std::span<const std::uint8_t> frame) override {
        mesh_->transport_.send(connection_->id, frame);
    }
    void send_datagram(std::span<const std::uint8_t> datagram) override {
        mesh_->transport_.send_datagram(connection_->id, datagram);
    }
    void set_input_destination_port(std::uint16_t port) override {
        mesh_->transport_.set_input_destination(connection_->id, port);
    }
    void accept_input_source() override { mesh_->transport_.accept_input_source(connection_->id); }
    [[nodiscard]] std::uint16_t local_input_port() override {
        return mesh_->transport_.input_port(connection_->id);
    }
    [[nodiscard]] std::uint16_t local_listen_port() const override {
        return mesh_->transport_.listen_port();
    }
    void close() override {
        if (!connection_->closed) {
            connection_->closed = true;
            mesh_->transport_.close(connection_->id);
        }
    }

private:
    Mesh* mesh_;             // never null; the mesh owns the connection that owns this adapter
    Connection* connection_; // never null
};

// --- Construction --------------------------------------------------------------------------------

Mesh::Mesh(MeshConfig config, crypto::KeyPair identity, crypto::RandomSource& random,
           MeshObserver& observer, MeshTransport& transport)
    : config_(std::move(config)), identity_(std::move(identity)),
      device_id_(crypto::device_id(identity_.public_key())), random_(random), observer_(observer),
      transport_(transport) {}

Mesh::~Mesh() = default;

void Mesh::log(LogLevel level, std::string_view message) {
    observer_.on_log(level, message);
}

Mesh::Connection& Mesh::add_connection(bool outbound, TimePoint now) {
    auto connection = std::make_unique<Connection>();
    connection->id = next_id_++;
    connection->outbound = outbound;
    connection->opened_at = now;
    Connection& added = *connection;
    connections_.emplace(added.id, std::move(connection));
    return added;
}

void Mesh::start_control(Connection& connection, session::SessionRole role, TimePoint /*now*/) {
    connection.kind = Connection::Kind::Control;
    connection.adapter = std::make_unique<ConnectionAdapter>(*this, connection);
    const session::SessionConfig config{.platform = config_.platform,
                                        .device_name = config_.device_name,
                                        .group_id = config_.group_id,
                                        .capabilities = config_.capabilities,
                                        .tunables = config_.tunables.session};
    connection.engine = std::make_unique<session::SessionEngine>(
        role, config, identity_, random_, *connection.adapter, *connection.adapter);
}

Mesh::Connection* Mesh::find(ConnectionId id) {
    const auto it = connections_.find(id);
    return it == connections_.end() || it->second->closed ? nullptr : it->second.get();
}

template <typename Connections>
auto* Mesh::find_established(Connections& connections, const DeviceId& device) {
    for (auto& [id, connection] : connections) {
        if (!connection->closed && connection->kind == Connection::Kind::Control &&
            connection->connected_reported && connection->peer_known &&
            connection->peer == device) {
            return connection.get();
        }
    }
    return static_cast<decltype(connections.begin()->second.get())>(nullptr);
}

Mesh::Connection* Mesh::established_with(const DeviceId& device) {
    return find_established(connections_, device);
}

const Mesh::Connection* Mesh::established_with(const DeviceId& device) const {
    return find_established(connections_, device);
}

Mesh::Connection* Mesh::transfer_of(const DeviceId& device, std::uint32_t stream_id) {
    for (auto& [id, connection] : connections_) {
        if (!connection->closed && connection->kind == Connection::Kind::Transfer &&
            connection->peer_known && connection->peer == device &&
            connection->channel->stream_id() == stream_id) {
            return connection.get();
        }
    }
    return nullptr;
}

bool Mesh::has_session_with(const DeviceId& device) const {
    return std::ranges::any_of(connections_, [&](const auto& entry) {
        const Connection& connection = *entry.second;
        return !connection.closed && connection.kind == Connection::Kind::Control &&
               ((connection.peer_known && connection.peer == device) ||
                connection.expected == device);
    });
}

bool Mesh::is_connected(const DeviceId& device) const {
    return established_with(device) != nullptr;
}

std::vector<DeviceId> Mesh::connected_peers() const {
    std::vector<DeviceId> peers;
    for (const auto& [id, connection] : connections_) {
        if (!connection->closed && connection->kind == Connection::Kind::Control &&
            connection->connected_reported) {
            peers.push_back(connection->peer);
        }
    }
    return peers;
}

// --- Session events ------------------------------------------------------------------------------

TrustVerdict Mesh::trust_query(Connection& connection, const crypto::Key& peer_key) {
    const DeviceId device = crypto::device_id(peer_key);
    if (device == device_id_) {
        log(LogLevel::Warning, "Connected to this computer itself");
        return TrustVerdict{};
    }
    if (connection.expected && *connection.expected != device) {
        log(LogLevel::Warning,
            "Dialled " + short_id(*connection.expected) + " but reached " + short_id(device));
        return TrustVerdict{};
    }
    connection.peer = device;
    connection.peer_known = true;

    // One session per pair (PROTOCOL.md, "Session flow"): both sides apply the same rule.
    const auto initiator = [&](const Connection& c) { return c.outbound ? device_id_ : device; };
    for (auto& [id, other] : connections_) {
        if (other.get() == &connection || other->closed || other->duplicate ||
            other->kind != Connection::Kind::Control || !other->peer_known ||
            other->peer != device) {
            continue;
        }
        const DeviceId lower = std::min(device_id_, device);
        const bool keep_new =
            initiator(*other) == initiator(connection) || initiator(connection) == lower;
        Connection& loser = keep_new ? *other : connection;
        loser.duplicate = true;
        duplicates_.push_back(loser.id);
        log(LogLevel::Info, "Duplicate connection to " + short_id(device) + "; closing " +
                                std::to_string(loser.id));
    }
    if (connection.duplicate) {
        return TrustVerdict{.decision = TrustDecision::Unknown}; // closed before anything is sent
    }
    return observer_.on_trust_query(peer_key, connection.engine->peer_hello());
}

void Mesh::session_established(Connection& connection, const protocol::Capabilities& peer) {
    const DeviceId& device = connection.peer;
    connection.connected_reported = true;
    connection.next_stream_id = connection.engine->role() == session::SessionRole::Dialer ? 1U : 2U;
    if (const auto it = peers_.find(device); it != peers_.end()) {
        it->second.backoff = Microseconds{0};
    }
    log(LogLevel::Info, "Connected to " + short_id(device));
    observer_.on_peer_connected(device, connection.engine->peer_static_key(),
                                connection.engine->peer_hello(), peer);
}

void Mesh::session_state(Connection& connection, session::SessionState state,
                         std::optional<Error> reason, TimePoint now) {
    if (state != session::SessionState::Closed) {
        return;
    }
    connection.closed = true;
    close_children(connection.id, reason.value_or(Error::ConnectionClosed));
    if (connection.connected_reported) {
        log(LogLevel::Info, "Disconnected from " + short_id(connection.peer));
        observer_.on_peer_disconnected(connection.peer, reason, connection.engine->peer_bye());
    } else if (connection.pairing) {
        observer_.on_pairing_failed(connection.id, reason.value_or(Error::ConnectionClosed));
    }

    const std::optional<DeviceId> device =
        connection.peer_known ? std::optional(connection.peer) : connection.expected;
    if (!device || connection.duplicate) {
        return; // a duplicate leaves the other connection in place
    }
    const auto it = peers_.find(*device);
    if (it == peers_.end()) {
        return;
    }
    if (blocks_redial(reason)) {
        it->second.refused = true;
    } else if (connection.engine->peer_bye() != protocol::ByeReason::Duplicate) {
        schedule_retry(it->second, now);
    }
}

// --- Transport events ----------------------------------------------------------------------------

std::optional<ConnectionId> Mesh::on_accepted(TimePoint now) {
    now_ = now;
    const auto pending = std::ranges::count_if(connections_, [](const auto& entry) {
        return !entry.second->closed && entry.second->kind == Connection::Kind::Pending;
    });
    if (std::cmp_greater_equal(pending, config_.tunables.max_pending_inbound)) {
        log(LogLevel::Warning, "Refused a connection: too many unidentified ones");
        return std::nullopt;
    }
    Connection& connection = add_connection(false, now);
    connection.connected = true;
    return connection.id;
}

void Mesh::on_connected(ConnectionId id, TimePoint now) {
    now_ = now;
    if (Connection* connection = find(id); connection != nullptr) {
        connection->connected = true;
        if (connection->kind == Connection::Kind::Control) {
            connection->engine->on_connected(now);
        } else if (connection->kind == Connection::Kind::Transfer) {
            const auto hello = connection->channel->hello_frame(connection->peer_token);
            if (!hello) {
                close_transfer_connection(*connection, hello.error());
            } else {
                transport_.send(id, hello.value());
                observer_.on_transfer_writable(connection->peer, connection->channel->stream_id());
            }
        }
    }
    settle(now);
}

void Mesh::on_bytes(ConnectionId id, std::span<const std::uint8_t> bytes, TimePoint now) {
    now_ = now;
    if (Connection* connection = find(id); connection != nullptr) {
        switch (connection->kind) {
        case Connection::Kind::Pending:
            connection->pending.insert(connection->pending.end(), bytes.begin(), bytes.end());
            handle_first_frame(*connection, now);
            break;
        case Connection::Kind::Control:
            connection->engine->on_control_bytes(bytes, now);
            break;
        case Connection::Kind::Transfer:
            transfer_bytes(*connection, bytes);
            break;
        }
    }
    settle(now);
}

void Mesh::handle_first_frame(Connection& connection, TimePoint now) {
    std::size_t frame_size = 0;
    switch (classify_first_frame(connection.pending, frame_size)) {
    case FirstFrame::Incomplete:
        return;
    case FirstFrame::Invalid:
        log(LogLevel::Warning, "Closed a connection with an invalid first frame");
        connection.closed = true;
        transport_.close(connection.id);
        return;
    case FirstFrame::Control: {
        const std::vector<std::uint8_t> bytes = std::move(connection.pending);
        start_control(connection, session::SessionRole::Listener, now);
        connection.engine->on_connected(now);
        connection.engine->on_control_bytes(bytes, now);
        return;
    }
    case FirstFrame::Transfer: {
        const std::vector<std::uint8_t> bytes = std::move(connection.pending);
        const std::span<const std::uint8_t> all(bytes);
        accept_transfer(connection, all.first(frame_size), all.subspan(frame_size), now);
        return;
    }
    }
}

void Mesh::accept_transfer(Connection& connection, std::span<const std::uint8_t> hello_frame,
                           std::span<const std::uint8_t> rest, TimePoint /*now*/) {
    const auto refuse = [&](std::string_view why) {
        log(LogLevel::Warning, std::string("Refused a transfer stream: ") + std::string(why));
        connection.closed = true;
        transport_.close(connection.id);
    };
    const auto message = protocol::decode_transfer(hello_frame.subspan(session::kFrameHeaderSize));
    const auto* hello = message ? std::get_if<protocol::TransferHello>(&message.value()) : nullptr;
    if (hello == nullptr) {
        refuse("the first frame is not a valid TransferHello");
        return;
    }
    Connection* control = nullptr;
    for (auto& [id, candidate] : connections_) {
        if (!candidate->closed && candidate->kind == Connection::Kind::Control &&
            candidate->connected_reported &&
            candidate->engine->local_transfer_token() == hello->transfer_token) {
            control = candidate.get();
            break;
        }
    }
    if (control == nullptr) {
        refuse("unknown token");
        return;
    }
    // The peer opened it: odd IDs come from the session's initiator, even ones from the responder.
    const bool peer_initiated = control->engine->role() == session::SessionRole::Listener;
    const bool odd = hello->stream_id % 2 == 1;
    if (odd != peer_initiated || control->streams.contains(hello->stream_id)) {
        refuse("stream ID of the wrong side or already used");
        return;
    }
    const auto keys = control->engine->transfer_keys(hello->stream_id);
    if (!keys) {
        refuse("the session has no keys");
        return;
    }
    control->streams.insert(hello->stream_id);
    connection.kind = Connection::Kind::Transfer;
    connection.control = control->id;
    connection.peer = control->peer;
    connection.peer_known = true;
    connection.channel =
        std::make_unique<TransferChannel>(hello->stream_id, keys->first, keys->second);
    observer_.on_transfer_opened(connection.peer, hello->stream_id);
    if (!rest.empty() && !connection.closed) {
        transfer_bytes(connection, rest);
    }
}

void Mesh::transfer_bytes(Connection& connection, std::span<const std::uint8_t> bytes) {
    const DeviceId device = connection.peer;
    const std::uint32_t stream_id = connection.channel->stream_id();
    const auto received =
        connection.channel->receive(bytes, [&](const protocol::TransferMessage& message) {
            observer_.on_transfer_message(device, stream_id, message);
        });
    if (!received) {
        log(LogLevel::Warning, "Transfer stream " + std::to_string(stream_id) + " failed, error " +
                                   std::to_string(static_cast<int>(received.error())));
        close_transfer_connection(connection, received.error());
    }
}

void Mesh::close_transfer_connection(Connection& connection, std::optional<Error> reason) {
    if (connection.closed) {
        return;
    }
    connection.closed = true;
    transport_.close(connection.id);
    observer_.on_transfer_closed(connection.peer, connection.channel->stream_id(), reason);
}

void Mesh::close_children(ConnectionId control, std::optional<Error> reason) {
    for (auto& [id, connection] : connections_) {
        if (connection->kind == Connection::Kind::Transfer && connection->control == control) {
            close_transfer_connection(*connection, reason);
        }
    }
}

void Mesh::on_datagram(ConnectionId id, std::span<std::uint8_t> datagram, TimePoint now) {
    now_ = now;
    if (const Connection* connection = find(id);
        connection != nullptr && connection->kind == Connection::Kind::Control) {
        connection->engine->on_datagram(datagram, now);
    }
    settle(now);
}

void Mesh::on_sent(ConnectionId id) {
    Connection* connection = find(id);
    if (connection == nullptr || connection->kind != Connection::Kind::Transfer ||
        !connection->blocked ||
        transport_.queued_bytes(id) >= config_.tunables.transfer_low_water) {
        return;
    }
    connection->blocked = false;
    observer_.on_transfer_writable(connection->peer, connection->channel->stream_id());
}

void Mesh::on_closed(ConnectionId id, Error reason, TimePoint now) {
    now_ = now;
    if (Connection* connection = find(id); connection != nullptr) {
        switch (connection->kind) {
        case Connection::Kind::Pending:
            connection->closed = true;
            break;
        case Connection::Kind::Control:
            connection->engine->on_transport_closed(reason);
            break;
        case Connection::Kind::Transfer:
            // The peer ending a stream is orderly; what was transferred is judged by its
            // messages (ContentEnd).
            close_transfer_connection(*connection, reason == Error::ConnectionClosed
                                                       ? std::nullopt
                                                       : std::optional<Error>(reason));
            break;
        }
    }
    settle(now);
}

// --- Time and dialling ---------------------------------------------------------------------------

bool Mesh::wants_session(const Peer& peer) const {
    return peer.discovered && peer.address && !peer.refused && !peer.held &&
           !is_zero(config_.group_id) && peer.group == config_.group_id;
}

TimePoint Mesh::dial_due(const Peer& peer) const {
    // The lower device ID dials; the higher one only when nothing has happened for a while.
    if (device_id_ < peer.id) {
        return peer.next_dial;
    }
    return std::max(peer.next_dial, peer.discovered_at + config_.tunables.dial_fallback_delay);
}

void Mesh::schedule_retry(Peer& peer, TimePoint now) const {
    peer.backoff = peer.backoff == Microseconds{0}
                       ? config_.tunables.first_retry
                       : std::min(peer.backoff * 2, config_.tunables.max_retry);
    peer.next_dial = now + peer.backoff;
}

void Mesh::dial_due_peers(TimePoint now) {
    for (auto& [device, peer] : peers_) {
        if (!peer.address || !wants_session(peer) || has_session_with(device) ||
            now < dial_due(peer)) {
            continue;
        }
        const PeerAddress address = *peer.address;
        Connection& connection = add_connection(true, now);
        connection.expected = device;
        start_control(connection, session::SessionRole::Dialer, now);
        log(LogLevel::Info, "Dialling " + short_id(device));
        // A retry is scheduled when this attempt ends without a session.
        peer.next_dial = now + std::max(peer.backoff, config_.tunables.first_retry);
        transport_.dial(connection.id, address);
    }
}

void Mesh::on_timer(TimePoint now) {
    now_ = now;
    for (auto& [id, connection] : connections_) {
        if (connection->closed) {
            continue;
        }
        if (connection->kind == Connection::Kind::Control) {
            connection->engine->on_timer(now);
        } else if (connection->kind == Connection::Kind::Pending &&
                   now - connection->opened_at >= config_.tunables.first_frame_timeout) {
            log(LogLevel::Warning, "Closed a connection that sent no first frame");
            connection->closed = true;
            transport_.close(id);
        }
    }
    dial_due_peers(now);
    settle(now);
}

TimePoint Mesh::next_wakeup() const {
    TimePoint next = TimePoint::max();
    for (const auto& [id, connection] : connections_) {
        if (connection->closed) {
            continue;
        }
        if (connection->kind == Connection::Kind::Control) {
            next = std::min(next, connection->engine->next_wakeup());
        } else if (connection->kind == Connection::Kind::Pending) {
            next = std::min(next, connection->opened_at + config_.tunables.first_frame_timeout);
        }
    }
    for (const auto& [device, peer] : peers_) {
        if (wants_session(peer) && !has_session_with(device)) {
            next = std::min(next, dial_due(peer));
        }
    }
    return next;
}

void Mesh::settle(TimePoint now) {
    if (settling_) {
        return;
    }
    settling_ = true;
    while (!duplicates_.empty() || !deferred_.empty()) {
        while (!duplicates_.empty()) {
            const ConnectionId id = duplicates_.back();
            duplicates_.pop_back();
            if (const Connection* connection = find(id);
                connection != nullptr && connection->kind == Connection::Kind::Control) {
                connection->engine->close(protocol::ByeReason::Duplicate, now);
            }
        }
        // Observers' deferred work may call entry points; their own settle() returns at once and
        // this loop picks up what they leave.
        auto work = std::move(deferred_);
        deferred_.clear();
        for (auto& item : work) {
            item(*this, now);
        }
    }
    std::erase_if(connections_, [](const auto& entry) { return entry.second->closed; });
    settling_ = false;
}

// --- Discovery -----------------------------------------------------------------------------------

void Mesh::peer_discovered(const DeviceId& device, const protocol::Id& group,
                           const PeerAddress& address, TimePoint now) {
    now_ = now;
    if (device == device_id_) {
        return; // this computer's own advertisement
    }
    Peer& peer = peers_[device];
    const bool changed = !peer.discovered || peer.address != address || peer.group != group;
    peer.id = device;
    peer.group = group;
    peer.address = address;
    if (!peer.discovered) {
        peer.discovered_at = now;
    }
    peer.discovered = true;
    peer.refused = false;
    peer.held = false;
    if (changed) {
        peer.next_dial = now;
        peer.backoff = Microseconds{0};
    }
    dial_due_peers(now);
    settle(now);
}

void Mesh::peer_lost(const DeviceId& device, TimePoint now) {
    now_ = now;
    // A session stays as long as the peer answers; only dialling stops.
    if (const auto it = peers_.find(device); it != peers_.end()) {
        it->second.discovered = false;
    }
    settle(now);
}

// --- Commands ------------------------------------------------------------------------------------

ConnectionId Mesh::pair_with(const PeerAddress& address, TimePoint now) {
    now_ = now;
    Connection& connection = add_connection(true, now);
    connection.pairing = true;
    start_control(connection, session::SessionRole::Dialer, now);
    const ConnectionId id = connection.id;
    log(LogLevel::Info, "Dialling " + address.host + " to pair");
    transport_.dial(id, address);
    settle(now);
    return id;
}

Status<Error> Mesh::confirm_pairing(ConnectionId id, bool accepted, TimePoint now) {
    now_ = now;
    const Connection* connection = find(id);
    auto result = connection != nullptr && connection->kind == Connection::Kind::Control
                      ? connection->engine->confirm_pairing(accepted, now)
                      : Status<Error>::failure(Error::InvalidState);
    settle(now);
    return result;
}

void Mesh::defer(std::function<void(Mesh&, TimePoint)> work) {
    deferred_.push_back(std::move(work));
}

void Mesh::flush(TimePoint now) {
    now_ = now;
    settle(now);
}

void Mesh::trust_changed(TimePoint now) {
    now_ = now;
    for (auto& [device, peer] : peers_) {
        peer.refused = false;
    }
    dial_due_peers(now);
    settle(now);
}

void Mesh::update_features(const DeviceId& device, std::uint32_t features, TimePoint now) {
    now_ = now;
    if (const Connection* connection = established_with(device); connection != nullptr) {
        connection->engine->update_features(features, now);
    }
    settle(now);
}

void Mesh::set_group(const protocol::Id& group, TimePoint now) {
    now_ = now;
    config_.group_id = group;
    dial_due_peers(now);
    settle(now);
}

Status<Error> Mesh::send_message(const DeviceId& device, const protocol::ControlMessage& message,
                                 TimePoint now) {
    now_ = now;
    const Connection* connection = established_with(device);
    auto result = connection != nullptr ? connection->engine->send_message(message, now)
                                        : Status<Error>::failure(Error::InvalidState);
    settle(now);
    return result;
}

Status<Error> Mesh::send_motion(const DeviceId& device, const protocol::PointerMotion& motion,
                                TimePoint now) {
    now_ = now;
    const Connection* connection = established_with(device);
    auto result = connection != nullptr ? connection->engine->send_motion(motion, now)
                                        : Status<Error>::failure(Error::InvalidState);
    settle(now);
    return result;
}

Expected<std::uint32_t, Error> Mesh::open_transfer(const DeviceId& device, TimePoint now) {
    using Result = Expected<std::uint32_t, Error>;
    now_ = now;
    Connection* control = established_with(device);
    if (control == nullptr || !control->engine->peer_setup()) {
        return Result::failure(Error::InvalidState);
    }
    if (control->next_stream_id > std::numeric_limits<std::uint32_t>::max() - 2) {
        return Result::failure(Error::NonceExhausted);
    }
    const std::uint32_t stream_id = control->next_stream_id;
    control->next_stream_id += 2;
    control->streams.insert(stream_id);
    const auto keys = control->engine->transfer_keys(stream_id);
    const auto& setup = control->engine->peer_setup();
    if (!keys || !setup) {
        return Result::failure(Error::InvalidState);
    }

    Connection& connection = add_connection(true, now);
    connection.kind = Connection::Kind::Transfer;
    connection.control = control->id;
    connection.peer = device;
    connection.peer_known = true;
    connection.peer_token = setup->transfer_token;
    connection.channel = std::make_unique<TransferChannel>(stream_id, keys->first, keys->second);
    transport_.dial_peer_of(connection.id, control->id, setup->listen_port);
    settle(now);
    return stream_id;
}

Status<Error> Mesh::send_transfer(const DeviceId& device, std::uint32_t stream_id,
                                  const protocol::TransferMessage& message, TimePoint now) {
    now_ = now;
    Connection* connection = transfer_of(device, stream_id);
    if (connection == nullptr || !connection->connected) {
        return Status<Error>::failure(Error::InvalidState);
    }
    if (transport_.queued_bytes(connection->id) >= config_.tunables.transfer_high_water) {
        connection->blocked = true;
        return Status<Error>::failure(Error::PeerBusy);
    }
    const auto frame = connection->channel->seal(message);
    if (!frame) {
        return Status<Error>::failure(frame.error());
    }
    transport_.send(connection->id, frame.value());
    if (transport_.queued_bytes(connection->id) >= config_.tunables.transfer_high_water) {
        connection->blocked = true;
    }
    settle(now);
    return Status<Error>::ok();
}

void Mesh::close_transfer(const DeviceId& device, std::uint32_t stream_id, TimePoint now) {
    now_ = now;
    if (Connection* connection = transfer_of(device, stream_id); connection != nullptr) {
        connection->closed = true;
        transport_.close(connection->id);
    }
    settle(now);
}

void Mesh::disconnect(const DeviceId& device, protocol::ByeReason reason, TimePoint now) {
    now_ = now;
    if (const auto it = peers_.find(device); it != peers_.end()) {
        it->second.held = true;
    }
    for (auto& [id, connection] : connections_) {
        if (!connection->closed && connection->kind == Connection::Kind::Control &&
            ((connection->peer_known && connection->peer == device) ||
             connection->expected == device)) {
            connection->engine->close(reason, now);
        }
    }
    settle(now);
}

void Mesh::close_all(protocol::ByeReason reason, TimePoint now) {
    now_ = now;
    for (auto& [device, peer] : peers_) {
        peer.held = true;
    }
    for (auto& [id, connection] : connections_) {
        if (connection->closed) {
            continue;
        }
        if (connection->kind == Connection::Kind::Control) {
            connection->engine->close(reason, now);
        } else if (connection->kind == Connection::Kind::Pending) {
            connection->closed = true;
            transport_.close(id);
        }
    }
    settle(now);
}

} // namespace osdrop::mesh
