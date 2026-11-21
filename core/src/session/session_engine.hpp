#pragma once

// One control connection between two computers, without I/O (docs/PROTOCOL.md, "Session flow"):
// bytes, datagrams, time and commands go in; bytes, datagrams and observer callbacks come out. The
// mesh runs one engine per connection; tests drive it with a simulated network and clock.
//
// Lifecycle: Connecting -> Handshaking -> AwaitingPairing -> Negotiating -> Established -> Closed.
// Every call must come from the same thread.

#include "crypto/input_crypto.hpp"
#include "crypto/noise.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/input_messages.hpp"
#include "protocol/wire.hpp"
#include "session/clock_offset.hpp"
#include "session/control_framer.hpp"
#include "session/session_config.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"
#include "util/time.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace osdrop::session {

enum class SessionState : std::uint8_t {
    Connecting = 1,
    Handshaking = 2,
    AwaitingPairing = 3,
    Negotiating = 4,
    Established = 5,
    Closed = 6,
};

enum class TrustDecision : std::uint8_t { Trusted = 1, Unknown = 2, Blocked = 3 };

/// What a computer decides about an authenticated peer.
struct TrustVerdict {
    TrustDecision decision = TrustDecision::Blocked;
    /// Sent to the peer in Error when the decision is Blocked.
    Error refusal = Error::NotTrusted;
    /// The Capabilities features this computer announces to the peer: its own, cleared of what
    /// it does not allow this peer (local permissions).
    std::uint32_t features = protocol::kKnownFeatures;
};
enum class LogLevel : std::uint8_t { Debug = 1, Info = 2, Warning = 3, Error = 4 };

struct SessionStats {
    std::uint32_t rtt_us = 0;
    std::int64_t clock_offset_us = 0; ///< peer clock minus local clock
    std::uint64_t datagrams_rejected = 0;
    std::uint64_t stale_motion_dropped = 0;
};

/// Callbacks from the engine. They run on the engine's thread and must not call back into the
/// engine, except where stated.
class SessionObserver {
public:
    virtual ~SessionObserver() = default;
    virtual void on_state(SessionState state, std::optional<Error> reason) = 0;
    virtual void on_peer_hello(const protocol::Hello& hello) = 0;
    /// The peer's static key is authenticated. Decide from in-memory data: the engine waits.
    [[nodiscard]] virtual TrustVerdict on_trust_query(const crypto::Key& peer_static_key) = 0;
    /// First pairing: show the code; the user's answer goes to SessionEngine::confirm_pairing
    /// (later, not from this callback).
    virtual void on_pairing_code(std::uint32_t code) = 0;
    /// Both users confirmed a first pairing.
    virtual void on_paired() = 0;
    /// The peer's capabilities are known and its input socket and listener announced; application
    /// messages may flow.
    virtual void on_established(const protocol::Capabilities& peer) = 0;
    /// An established peer announced new capabilities (its permissions for this computer changed).
    virtual void on_capabilities(const protocol::Capabilities& peer) = 0;
    /// The peer fell silent for unresponsive_after (false) or was heard from again (true).
    virtual void on_responsive(bool responsive) = 0;
    /// An application message (RosterState and later types) from an established peer.
    virtual void on_message(const protocol::ControlMessage& message) = 0;
    /// The newest pointer position; older and repeated datagrams never arrive here.
    virtual void on_motion(const protocol::PointerMotion& motion) = 0;
    virtual void on_stats(const SessionStats& stats) = 0;
    virtual void on_log(LogLevel level, std::string_view message) = 0;
};

/// Where the engine's output goes.
class SessionTransport {
public:
    virtual ~SessionTransport() = default;
    virtual void send_control(std::span<const std::uint8_t> frame) = 0;
    virtual void send_datagram(std::span<const std::uint8_t> datagram) = 0;
    /// Input datagrams go to this UDP port at the address of the control connection's peer.
    virtual void set_input_destination_port(std::uint16_t port) = 0;
    /// The datagram being processed came from the peer: send input datagrams to its source.
    virtual void accept_input_source() = 0;
    /// The UDP port of this connection's input socket, announced in SessionSetup.
    [[nodiscard]] virtual std::uint16_t local_input_port() = 0;
    /// The TCP port this computer accepts transfer streams on.
    [[nodiscard]] virtual std::uint16_t local_listen_port() const = 0;
    /// Closes the connection once the queued bytes are written.
    virtual void close() = 0;
};

enum class SessionRole : std::uint8_t {
    Dialer = 1,   ///< opened the TCP connection: the Noise initiator
    Listener = 2, ///< accepted it: the Noise responder
};

class SessionEngine {
public:
    SessionEngine(SessionRole role, SessionConfig config, crypto::KeyPair identity,
                  crypto::RandomSource& random, SessionObserver& observer,
                  SessionTransport& transport);
    ~SessionEngine();
    SessionEngine(const SessionEngine&) = delete;
    SessionEngine& operator=(const SessionEngine&) = delete;
    SessionEngine(SessionEngine&&) = delete;
    SessionEngine& operator=(SessionEngine&&) = delete;

    // --- From the transport ---
    /// The TCP connection is up; both sides send Hello.
    void on_connected(TimePoint now);
    void on_control_bytes(std::span<const std::uint8_t> bytes, TimePoint now);
    /// Decrypts in place.
    void on_datagram(std::span<std::uint8_t> datagram, TimePoint now);
    void on_timer(TimePoint now);
    void on_transport_closed(Error reason);

    // --- Commands ---
    [[nodiscard]] Status<Error> confirm_pairing(bool accepted, TimePoint now);
    /// An application message (RosterState and later types); Established only.
    [[nodiscard]] Status<Error> send_message(const protocol::ControlMessage& message,
                                             TimePoint now);
    /// One pointer position over the input socket; Established only.
    [[nodiscard]] Status<Error> send_motion(const protocol::PointerMotion& motion, TimePoint now);
    /// Changes the features announced to the peer; sent at once when established.
    void update_features(std::uint32_t features, TimePoint now);
    /// Sends Bye (when the channel is encrypted) and closes.
    void close(protocol::ByeReason reason, TimePoint now);

    // --- Queries ---
    [[nodiscard]] SessionState state() const { return state_; }
    [[nodiscard]] SessionRole role() const { return role_; }
    [[nodiscard]] bool keyed() const { return keys_.has_value(); }
    [[nodiscard]] bool responsive() const { return responsive_; }
    /// The peer's authenticated static key; valid once keyed().
    [[nodiscard]] const crypto::Key& peer_static_key() const { return peer_static_; }
    /// What the peer said in Hello; valid once has_peer_hello().
    [[nodiscard]] bool has_peer_hello() const { return peer_hello_received_; }
    [[nodiscard]] const protocol::Hello& peer_hello() const { return peer_hello_; }
    /// The Bye reason the peer gave, if it closed the session with Bye.
    [[nodiscard]] std::optional<protocol::ByeReason> peer_bye() const { return peer_bye_; }
    /// The token peers present in a TransferHello to this computer (from our SessionSetup).
    [[nodiscard]] const protocol::Id& local_transfer_token() const { return transfer_token_; }
    /// What the peer announced in SessionSetup; Established only.
    [[nodiscard]] const std::optional<protocol::SessionSetup>& peer_setup() const {
        return peer_setup_;
    }
    /// The keys of one transfer stream: (send, receive). Keyed only.
    [[nodiscard]] std::optional<std::pair<crypto::Key, crypto::Key>>
    transfer_keys(std::uint32_t stream_id) const;
    /// When on_timer next has something to do.
    [[nodiscard]] TimePoint next_wakeup() const;
    [[nodiscard]] SessionStats stats() const;

private:
    void handle_frame(std::span<const std::uint8_t> payload, TimePoint now);
    void handle_message(const protocol::ControlMessage& message, TimePoint now);
    [[nodiscard]] bool is_expected(protocol::ControlType type) const;
    /// The latest sign of life: control bytes or an input datagram.
    [[nodiscard]] TimePoint last_heard() const;
    void handle_hello(const protocol::Hello& hello, TimePoint now);
    void handle_handshake(const protocol::HandshakeMessage& message, TimePoint now);
    void handshake_complete(TimePoint now);
    void handle_pairing_confirm(const protocol::PairingConfirm& message, TimePoint now);
    void finish_pairing_if_agreed(TimePoint now);
    void enter_negotiation(TimePoint now);
    void establish_if_ready(TimePoint now);
    void handle_ping(const protocol::Ping& ping, TimePoint now);
    void handle_pong(const protocol::Pong& pong, TimePoint now);
    void handle_peer_error(const protocol::PeerError& error);
    void handle_bye(const protocol::Bye& bye);
    void send(const protocol::ControlMessage& message, TimePoint now);
    [[nodiscard]] protocol::Capabilities offered_capabilities() const;
    [[nodiscard]] bool seal_and_send(const protocol::InputDatagram& datagram);
    void send_input_hello();
    void fail(Error reason, bool tell_peer, TimePoint now);
    /// Closes without sending anything; the only way out when sending itself failed.
    void abort_session(std::optional<Error> reason);
    void set_state(SessionState state, std::optional<Error> reason = std::nullopt);
    void set_responsive(bool responsive);
    void log(LogLevel level, std::string_view message);
    [[nodiscard]] static std::uint64_t micros(TimePoint now);

    SessionRole role_;
    SessionConfig config_;
    crypto::KeyPair identity_;
    crypto::RandomSource& random_;
    SessionObserver& observer_;
    SessionTransport& transport_;

    SessionState state_ = SessionState::Connecting;
    bool closing_ = false;
    FrameDecoder frames_;
    std::vector<std::uint8_t> frame_buffer_;
    std::optional<crypto::HandshakeState> handshake_;
    std::optional<crypto::SessionKeys> keys_;
    std::unique_ptr<crypto::InputSealer> sealer_;
    std::unique_ptr<crypto::InputOpener> opener_;
    crypto::Key peer_static_{};
    protocol::Hello peer_hello_;
    bool peer_hello_received_ = false;
    std::optional<protocol::ByeReason> peer_bye_;

    TrustDecision trust_ = TrustDecision::Unknown;
    std::uint32_t features_ = protocol::kKnownFeatures; ///< mask for what this peer is offered
    bool local_decision_made_ = false;
    bool local_accepted_ = false;
    bool peer_accepted_ = false;

    protocol::Id input_token_{};
    protocol::Id transfer_token_{};
    std::optional<protocol::Capabilities> peer_capabilities_;
    std::optional<protocol::SessionSetup> peer_setup_;
    bool input_bound_ = false; ///< the peer's InputHello arrived
    std::uint64_t next_sequence_ = 0;
    std::optional<std::uint64_t> last_motion_sequence_;
    std::array<std::uint8_t, protocol::kMaxDatagramSize> datagram_{};

    TimePoint connected_at_;
    TimePoint keyed_at_;
    TimePoint last_sent_;
    TimePoint last_received_; ///< control bytes; the session times out without them
    TimePoint last_datagram_; ///< an input datagram that opened; keeps the peer responsive
    TimePoint next_ping_;
    TimePoint next_input_hello_;
    TimePoint next_stats_;
    bool responsive_ = true;
    std::uint32_t next_ping_id_ = 0;
    std::optional<Microseconds> rtt_;
    std::int64_t clock_offset_us_ = 0;
    ClockOffsetEstimator clock_offsets_;
    std::uint64_t datagrams_rejected_ = 0;
    std::uint64_t stale_motion_dropped_ = 0;
};

} // namespace osdrop::session
