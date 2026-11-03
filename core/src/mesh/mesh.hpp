#pragma once

// Every connection of one computer, without I/O (docs/PROTOCOL.md, "Session flow"): one session
// per group member, dialled by the side with the lower device ID; duplicates from simultaneous
// dials resolved the same way on both sides; reconnects with backoff; transfer streams opened on
// sessions. The Asio node drives it on its I/O thread; tests drive several meshes over a simulated
// network. Every call must come from the same thread. Observers must not call back into the mesh;
// they may defer() work instead, which runs before the entry point that called them returns.

#include "crypto/noise.hpp"
#include "mesh/transfer_channel.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/input_messages.hpp"
#include "protocol/transfer_messages.hpp"
#include "session/session_config.hpp"
#include "session/session_engine.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"
#include "util/time.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace osdrop::mesh {

using ConnectionId = std::uint64_t;
using DeviceId = protocol::Id;
using session::LogLevel;
using session::SessionStats;
using session::TrustDecision;
using session::TrustVerdict;

struct PeerAddress {
    std::string host;
    std::uint16_t port = 0;
    bool operator==(const PeerAddress&) const = default;
};

struct MeshTunables {
    session::SessionTunables session;
    /// Reconnect backoff: the first retry after this, doubling up to max_retry.
    Microseconds first_retry{250'000};
    Microseconds max_retry{30'000'000};
    /// The computer with the higher device ID dials too when no session exists this long after
    /// discovery (the lower one may not see it).
    Microseconds dial_fallback_delay{3'000'000};
    /// An accepted connection must complete its first frame within this time.
    Microseconds first_frame_timeout{10'000'000};
    std::size_t max_pending_inbound = 16;
    /// A transfer stream refuses more messages while this much is queued for writing, and reports
    /// it writable again below transfer_low_water.
    std::size_t transfer_high_water = std::size_t{4} * 1024 * 1024;
    std::size_t transfer_low_water = std::size_t{1} * 1024 * 1024;
};

struct MeshConfig {
    protocol::Platform platform = protocol::Platform::MacOS;
    std::string device_name;
    protocol::Id group_id{}; ///< all zeros when in no group
    protocol::Capabilities capabilities;
    MeshTunables tunables;
};

/// What the mesh needs from the network. Connection IDs come from the mesh.
class MeshTransport {
public:
    virtual ~MeshTransport() = default;
    /// Opens a TCP connection; the outcome is Mesh::on_connected or Mesh::on_closed.
    virtual void dial(ConnectionId id, const PeerAddress& address) = 0;
    /// As dial, to `port` at the remote address of connection `via`.
    virtual void dial_peer_of(ConnectionId id, ConnectionId via, std::uint16_t port) = 0;
    /// Queues bytes on a TCP connection.
    virtual void send(ConnectionId id, std::span<const std::uint8_t> bytes) = 0;
    /// Sends an input datagram from the connection's input socket.
    virtual void send_datagram(ConnectionId id, std::span<const std::uint8_t> datagram) = 0;
    /// The connection's input socket port, opened on first use; 0 on failure.
    [[nodiscard]] virtual std::uint16_t input_port(ConnectionId id) = 0;
    virtual void set_input_destination(ConnectionId id, std::uint16_t port) = 0;
    /// The datagram being delivered came from the peer: reply to its source.
    virtual void accept_input_source(ConnectionId id) = 0;
    [[nodiscard]] virtual std::size_t queued_bytes(ConnectionId id) const = 0;
    /// Stops or resumes reading from a TCP connection; while stopped, TCP holds the peer back.
    virtual void pause_reading(ConnectionId id, bool paused) = 0;
    /// Closes once queued bytes are written. Mesh::on_closed is not called for it.
    virtual void close(ConnectionId id) = 0;
    [[nodiscard]] virtual std::uint16_t listen_port() const = 0;
};

/// Callbacks from the mesh, on its thread.
class MeshObserver {
public:
    virtual ~MeshObserver() = default;
    /// The peer's static key is authenticated. Decide from in-memory data: the mesh waits.
    [[nodiscard]] virtual TrustVerdict on_trust_query(const crypto::Key& peer_key,
                                                      const protocol::Hello& hello) = 0;
    /// A first pairing: show the code; answer with Mesh::confirm_pairing.
    virtual void on_pairing_code(ConnectionId connection, std::uint32_t code,
                                 const crypto::Key& peer_key, const protocol::Hello& hello) = 0;
    /// Both users confirmed: the peer can be trusted from now on. Its session continues.
    virtual void on_paired(const crypto::Key& peer_key, const protocol::Hello& hello) = 0;
    /// A pairing connection ended before its session was established.
    virtual void on_pairing_failed(ConnectionId connection, Error reason) = 0;
    virtual void on_peer_connected(const DeviceId& device, const crypto::Key& peer_key,
                                   const protocol::Hello& hello,
                                   const protocol::Capabilities& capabilities) = 0;
    /// `reason` is empty for an orderly end; `bye` is the reason the peer gave, if it said Bye.
    virtual void on_peer_disconnected(const DeviceId& device, std::optional<Error> reason,
                                      std::optional<protocol::ByeReason> bye) = 0;
    /// A connected peer announced new capabilities.
    virtual void on_peer_capabilities(const DeviceId& device,
                                      const protocol::Capabilities& capabilities) = 0;
    /// Silent for unresponsive_after (false), or heard from again (true). Input to and from an
    /// unresponsive peer must stop.
    virtual void on_peer_responsive(const DeviceId& device, bool responsive) = 0;
    virtual void on_message(const DeviceId& device, const protocol::ControlMessage& message) = 0;
    virtual void on_motion(const DeviceId& device, const protocol::PointerMotion& motion) = 0;
    /// The peer opened a transfer stream to this computer.
    virtual void on_transfer_opened(const DeviceId& device, std::uint32_t stream_id) = 0;
    virtual void on_transfer_message(const DeviceId& device, std::uint32_t stream_id,
                                     const protocol::TransferMessage& message) = 0;
    /// A stream this computer opened is connected, or a full stream drained: send again.
    virtual void on_transfer_writable(const DeviceId& device, std::uint32_t stream_id) = 0;
    virtual void on_transfer_closed(const DeviceId& device, std::uint32_t stream_id,
                                    std::optional<Error> reason) = 0;
    virtual void on_stats(const DeviceId& device, const SessionStats& stats) = 0;
    virtual void on_log(LogLevel level, std::string_view message) = 0;
};

class Mesh {
public:
    Mesh(MeshConfig config, crypto::KeyPair identity, crypto::RandomSource& random,
         MeshObserver& observer, MeshTransport& transport);
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) = delete;
    Mesh& operator=(Mesh&&) = delete;

    [[nodiscard]] const DeviceId& device_id() const { return device_id_; }

    // --- From the transport ---
    /// A connection was accepted; returns its ID, or nothing when too many are still unidentified
    /// (the transport then closes it).
    [[nodiscard]] std::optional<ConnectionId> on_accepted(TimePoint now);
    void on_connected(ConnectionId id, TimePoint now);
    void on_bytes(ConnectionId id, std::span<const std::uint8_t> bytes, TimePoint now);
    void on_datagram(ConnectionId id, std::span<std::uint8_t> datagram, TimePoint now);
    /// A write completed; queued_bytes may have fallen.
    void on_sent(ConnectionId id);
    /// A dial failed or the peer closed the connection.
    void on_closed(ConnectionId id, Error reason, TimePoint now);
    void on_timer(TimePoint now);
    [[nodiscard]] TimePoint next_wakeup() const;

    // --- From discovery ---
    /// A computer advertises `group` (all zeros: none) at `address`.
    void peer_discovered(const DeviceId& device, const protocol::Id& group,
                         const PeerAddress& address, TimePoint now);
    void peer_lost(const DeviceId& device, TimePoint now);

    // --- Commands ---
    /// Dials `address` to pair with the computer there, whatever its group.
    ConnectionId pair_with(const PeerAddress& address, TimePoint now);
    [[nodiscard]] Status<Error> confirm_pairing(ConnectionId id, bool accepted, TimePoint now);
    /// Changes the group this computer advertises in Hello and dials members of.
    void set_group(const protocol::Id& group, TimePoint now);
    /// Who is trusted changed (a roster merge): peers refused before are dialled again.
    void trust_changed(TimePoint now);
    /// Changes the features announced to a connected peer (its permissions changed).
    void update_features(const DeviceId& device, std::uint32_t features, TimePoint now);
    /// Runs `work` after the current entry point's own work, before it returns. For observers.
    void defer(std::function<void(Mesh&, TimePoint)> work);
    /// Runs deferred work now; for callers outside any entry point (commands on other layers).
    void flush(TimePoint now);
    [[nodiscard]] Status<Error>
    send_message(const DeviceId& device, const protocol::ControlMessage& message, TimePoint now);
    [[nodiscard]] Status<Error> send_motion(const DeviceId& device,
                                            const protocol::PointerMotion& motion, TimePoint now);
    /// Opens a transfer stream to an established peer; on_transfer_writable follows when it is
    /// connected, on_transfer_closed if it fails.
    [[nodiscard]] Expected<std::uint32_t, Error> open_transfer(const DeviceId& device,
                                                               TimePoint now);
    /// Fails with PeerBusy while the stream's queue is full (wait for on_transfer_writable).
    [[nodiscard]] Status<Error> send_transfer(const DeviceId& device, std::uint32_t stream_id,
                                              const protocol::TransferMessage& message,
                                              TimePoint now);
    void close_transfer(const DeviceId& device, std::uint32_t stream_id, TimePoint now);
    /// Stops taking messages from a stream (the receiver cannot keep up) or resumes.
    void pause_transfer(const DeviceId& device, std::uint32_t stream_id, bool paused,
                        TimePoint now);
    /// Ends the session with a peer; it is not redialled until discovered again.
    void disconnect(const DeviceId& device, protocol::ByeReason reason, TimePoint now);
    /// Ends every session (sleep, quit); nothing is redialled until discovered again.
    void close_all(protocol::ByeReason reason, TimePoint now);

    [[nodiscard]] bool is_connected(const DeviceId& device) const;
    [[nodiscard]] std::vector<DeviceId> connected_peers() const;

private:
    class ConnectionAdapter;
    struct Connection;
    struct Peer;

    // Session events, from the adapters.
    TrustVerdict trust_query(Connection& connection, const crypto::Key& peer_key);
    void session_state(Connection& connection, session::SessionState state,
                       std::optional<Error> reason, TimePoint now);
    void session_established(Connection& connection, const protocol::Capabilities& peer);

    void handle_first_frame(Connection& connection, TimePoint now);
    void accept_transfer(Connection& connection, std::span<const std::uint8_t> hello_frame,
                         std::span<const std::uint8_t> rest, TimePoint now);
    void transfer_bytes(Connection& connection, std::span<const std::uint8_t> bytes);
    void close_transfer_connection(Connection& connection, std::optional<Error> reason);
    void close_children(ConnectionId control, std::optional<Error> reason);
    [[nodiscard]] TimePoint dial_due(const Peer& peer) const;
    void dial_due_peers(TimePoint now);
    [[nodiscard]] bool wants_session(const Peer& peer) const;
    [[nodiscard]] bool has_session_with(const DeviceId& device) const;
    void schedule_retry(Peer& peer, TimePoint now) const;
    template <typename Connections>
    static auto* find_established(Connections& connections, const DeviceId& device);
    [[nodiscard]] Connection* established_with(const DeviceId& device);
    [[nodiscard]] const Connection* established_with(const DeviceId& device) const;
    [[nodiscard]] Connection* find(ConnectionId id);
    [[nodiscard]] Connection* transfer_of(const DeviceId& device, std::uint32_t stream_id);
    Connection& add_connection(bool outbound, TimePoint now);
    void start_control(Connection& connection, session::SessionRole role, TimePoint now);
    /// Runs deferred closes and removes closed connections; ends every public entry point.
    void settle(TimePoint now);
    void log(LogLevel level, std::string_view message);

    MeshConfig config_;
    crypto::KeyPair identity_;
    DeviceId device_id_{};
    crypto::RandomSource& random_;
    MeshObserver& observer_;
    MeshTransport& transport_;

    ConnectionId next_id_ = 1;
    std::map<ConnectionId, std::unique_ptr<Connection>> connections_;
    std::map<DeviceId, Peer> peers_;
    std::vector<ConnectionId> duplicates_; // control connections to close as duplicates
    std::vector<std::function<void(Mesh&, TimePoint)>> deferred_;
    bool settling_ = false;
    TimePoint now_; // the time of the entry point being run, for session callbacks
};

} // namespace osdrop::mesh
