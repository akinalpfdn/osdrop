#include "session/session_engine.hpp"

#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>

namespace osdrop::session {

namespace {

using protocol::ControlMessage;
using protocol::ControlType;

/// Application messages start at RosterState; the session layer only routes them.
constexpr auto kFirstApplicationType = static_cast<std::uint8_t>(ControlType::RosterState);

template <typename... Visitors>
struct Overloaded : Visitors... { // NOLINT(misc-multiple-inheritance): the standard visitor idiom
    using Visitors::operator()...;
};

bool is_application(ControlType type) {
    return static_cast<std::uint8_t>(type) >= kFirstApplicationType;
}

/// Before the handshake completes only these travel in plaintext.
bool allowed_in_plaintext(ControlType type) {
    return type == ControlType::Hello || type == ControlType::HandshakeMessage ||
           type == ControlType::Error;
}

/// The local reason for an Error message from the peer: the codes the UI explains as such, any
/// other as a generic peer error.
Error reason_for_peer_error(std::uint16_t code) {
    for (const Error known :
         {Error::UnsupportedVersion, Error::PairingRejected, Error::NotTrusted, Error::PeerBusy,
          Error::NotPermitted, Error::GroupFull, Error::InAnotherGroup}) {
        if (code == static_cast<std::uint16_t>(known)) {
            return known;
        }
    }
    return Error::PeerError;
}

const char* state_name(SessionState state) {
    switch (state) {
    case SessionState::Connecting:
        return "connecting";
    case SessionState::Handshaking:
        return "handshaking";
    case SessionState::AwaitingPairing:
        return "awaiting pairing";
    case SessionState::Negotiating:
        return "negotiating";
    case SessionState::Established:
        return "established";
    case SessionState::Closed:
        return "closed";
    }
    return "unknown";
}

bool is_zero_id(const protocol::Id& id) {
    return std::ranges::all_of(id, [](std::uint8_t byte) { return byte == 0; });
}

} // namespace

SessionEngine::SessionEngine(SessionRole role, SessionConfig config, crypto::KeyPair identity,
                             crypto::RandomSource& random, SessionObserver& observer,
                             SessionTransport& transport)
    : role_(role), config_(std::move(config)), identity_(std::move(identity)), random_(random),
      observer_(observer), transport_(transport) {}

SessionEngine::~SessionEngine() = default;

std::uint64_t SessionEngine::micros(TimePoint now) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<Microseconds>(now.time_since_epoch()).count());
}

void SessionEngine::log(LogLevel level, std::string_view message) {
    observer_.on_log(level, message);
}

void SessionEngine::set_state(SessionState state, std::optional<Error> reason) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    std::string line = std::string("Session ") + state_name(state);
    if (reason) {
        line += " (error " + std::to_string(static_cast<int>(*reason)) + ")";
    }
    log(LogLevel::Info, line);
    observer_.on_state(state, reason);
}

void SessionEngine::set_responsive(bool responsive) {
    if (responsive_ == responsive) {
        return;
    }
    responsive_ = responsive;
    log(responsive ? LogLevel::Info : LogLevel::Warning,
        responsive ? "Peer responsive again" : "Peer unresponsive");
    observer_.on_responsive(responsive);
}

void SessionEngine::abort_session(std::optional<Error> reason) {
    if (state_ == SessionState::Closed) {
        return;
    }
    closing_ = true;
    transport_.close();
    set_state(SessionState::Closed, reason);
}

void SessionEngine::fail(Error reason, bool tell_peer, TimePoint now) {
    if (state_ == SessionState::Closed || closing_) {
        return;
    }
    log(LogLevel::Warning, "Session failed with error " + std::to_string(static_cast<int>(reason)));
    if (tell_peer) {
        send(protocol::PeerError{.code = static_cast<std::uint16_t>(reason)}, now);
    }
    abort_session(reason);
}

void SessionEngine::send(const ControlMessage& message, TimePoint now) {
    const auto plaintext = protocol::encode_control(message);
    if (!plaintext) {
        log(LogLevel::Error, "Refused to send an invalid control message");
        abort_session(plaintext.error());
        return;
    }
    if (keys_) {
        const auto ciphertext = keys_->control_send.encrypt_with_ad({}, plaintext.value());
        if (!ciphertext) {
            abort_session(ciphertext.error());
            return;
        }
        write_frame(ciphertext.value(), frame_buffer_);
    } else {
        write_frame(plaintext.value(), frame_buffer_);
    }
    transport_.send_control(frame_buffer_);
    last_sent_ = now;
}

// --- Control channel -----------------------------------------------------------------------------

void SessionEngine::on_connected(TimePoint now) {
    if (state_ != SessionState::Connecting) {
        return;
    }
    connected_at_ = now;
    last_received_ = now;
    set_state(SessionState::Handshaking);
    send(protocol::Hello{.platform = config_.platform,
                         .protocol_minor = protocol::kMinorVersion,
                         .group_id = config_.group_id,
                         .device_name = config_.device_name},
         now);
}

void SessionEngine::on_control_bytes(std::span<const std::uint8_t> bytes, TimePoint now) {
    if (state_ == SessionState::Closed) {
        return;
    }
    frames_.append(bytes);
    while (state_ != SessionState::Closed) {
        const auto frame = frames_.next();
        if (!frame) {
            fail(frame.error(), false, now);
            return;
        }
        if (frame.value().empty()) {
            return;
        }
        handle_frame(frame.value(), now);
    }
}

void SessionEngine::handle_frame(std::span<const std::uint8_t> payload, TimePoint now) {
    last_received_ = now;
    std::vector<std::uint8_t> decrypted;
    std::span<const std::uint8_t> plaintext = payload;
    if (keys_) {
        auto opened = keys_->control_receive.decrypt_with_ad({}, payload);
        if (!opened) {
            fail(opened.error(), false, now);
            return;
        }
        decrypted = std::move(opened.value());
        plaintext = decrypted;
        set_responsive(true);
    }
    const auto message = protocol::decode_control(plaintext);
    if (!message) {
        if (message.error() == Error::UnsupportedVersion) {
            fail(Error::UnsupportedVersion, true, now);
        } else {
            log(LogLevel::Warning, "Malformed control message, error " +
                                       std::to_string(static_cast<int>(message.error())));
            fail(Error::UnexpectedMessage, true, now);
        }
        return;
    }
    if (!keys_ && !allowed_in_plaintext(protocol::type_of(message.value()))) {
        fail(Error::UnexpectedMessage, true, now);
        return;
    }
    handle_message(message.value(), now);
}

bool SessionEngine::is_expected(ControlType type) const {
    if (is_application(type)) {
        return state_ == SessionState::Established;
    }
    switch (type) {
    case ControlType::Hello:
        return state_ == SessionState::Handshaking && !peer_hello_received_;
    case ControlType::HandshakeMessage:
        return state_ == SessionState::Handshaking && peer_hello_received_ && handshake_;
    case ControlType::PairingConfirm:
        return state_ == SessionState::AwaitingPairing && !peer_accepted_;
    case ControlType::Capabilities: // again when the peer's permissions for this computer change
        return (state_ == SessionState::Negotiating && !peer_capabilities_) ||
               state_ == SessionState::Established;
    case ControlType::SessionSetup:
        return state_ == SessionState::Negotiating && !peer_setup_;
    case ControlType::Ping:
    case ControlType::Pong:
    case ControlType::Heartbeat:
    case ControlType::Error:
    case ControlType::Bye:
        return true; // encryption is checked before dispatch
    default:
        return false;
    }
}

void SessionEngine::handle_message(const ControlMessage& message, TimePoint now) {
    if (!is_expected(protocol::type_of(message))) {
        fail(Error::UnexpectedMessage, true, now);
        return;
    }
    if (is_application(protocol::type_of(message))) {
        observer_.on_message(message);
        return;
    }
    std::visit(Overloaded{
                   [&](const protocol::Hello& m) { handle_hello(m, now); },
                   [&](const protocol::HandshakeMessage& m) { handle_handshake(m, now); },
                   [&](const protocol::PairingConfirm& m) { handle_pairing_confirm(m, now); },
                   [&](const protocol::Capabilities& m) {
                       const bool update = state_ == SessionState::Established;
                       peer_capabilities_ = m;
                       if (update) {
                           observer_.on_capabilities(m);
                       } else {
                           establish_if_ready(now);
                       }
                   },
                   [&](const protocol::SessionSetup& m) {
                       peer_setup_ = m;
                       transport_.set_input_destination_port(m.udp_port);
                       send_input_hello();
                       next_input_hello_ = now + config_.tunables.input_hello_interval;
                       establish_if_ready(now);
                   },
                   [&](const protocol::Ping& m) { handle_ping(m, now); },
                   [&](const protocol::Pong& m) { handle_pong(m, now); },
                   [&](const protocol::PeerError& m) { handle_peer_error(m); },
                   [&](const protocol::Bye& m) { handle_bye(m); },
                   [&](const auto& /*heartbeat or routed above*/) {},
               },
               message);
}

void SessionEngine::handle_hello(const protocol::Hello& hello, TimePoint now) {
    peer_hello_ = hello;
    peer_hello_received_ = true;
    observer_.on_peer_hello(hello);
    if (state_ == SessionState::Closed) {
        return;
    }
    const bool initiator = role_ == SessionRole::Dialer;
    handshake_.emplace(initiator ? crypto::HandshakeRole::Initiator
                                 : crypto::HandshakeRole::Responder,
                       identity_, crypto::prologue(), random_);
    if (initiator) {
        const auto message = handshake_->write_message({});
        if (!message) {
            fail(message.error(), false, now);
            return;
        }
        send(protocol::HandshakeMessage{.step = 1, .payload = message.value()}, now);
    }
}

void SessionEngine::handle_handshake(const protocol::HandshakeMessage& message, TimePoint now) {
    const bool initiator = role_ == SessionRole::Dialer;
    const bool expected_step =
        initiator ? message.step == 2 : (message.step == 1 || message.step == 3);
    if (!expected_step || !handshake_) {
        fail(Error::HandshakeOutOfOrder, true, now);
        return;
    }
    if (const auto payload = handshake_->read_message(message.payload); !payload) {
        fail(payload.error(), true, now);
        return;
    }
    if (handshake_->is_complete()) { // responder after message 3
        handshake_complete(now);
        return;
    }
    const auto reply = handshake_->write_message({});
    if (!reply) {
        fail(reply.error(), true, now);
        return;
    }
    const bool complete = handshake_->is_complete(); // initiator after writing message 3
    send(protocol::HandshakeMessage{.step = static_cast<std::uint8_t>(message.step + 1),
                                    .payload = reply.value()},
         now);
    if (complete && state_ != SessionState::Closed) {
        handshake_complete(now);
    }
}

void SessionEngine::handshake_complete(TimePoint now) {
    if (!handshake_) {
        fail(Error::HandshakeOutOfOrder, false, now);
        return;
    }
    auto keys = handshake_->finish();
    handshake_.reset();
    if (!keys) {
        fail(keys.error(), false, now);
        return;
    }
    const crypto::SessionKeys& session = keys_.emplace(std::move(keys.value()));
    sealer_ = std::make_unique<crypto::InputSealer>(session.input_send);
    opener_ = std::make_unique<crypto::InputOpener>(session.input_receive);
    peer_static_ = session.remote_static;
    keyed_at_ = now;
    last_received_ = now;
    next_ping_ = now;
    next_stats_ = now + config_.tunables.stats_interval;
    set_state(SessionState::AwaitingPairing);

    const TrustVerdict verdict = observer_.on_trust_query(peer_static_);
    if (state_ == SessionState::Closed) {
        return;
    }
    trust_ = verdict.decision;
    features_ = verdict.features;
    if (trust_ == TrustDecision::Blocked) {
        fail(verdict.refusal, true, now);
        return;
    }
    if (trust_ == TrustDecision::Trusted) {
        local_decision_made_ = true;
        local_accepted_ = true;
        send(protocol::PairingConfirm{.decision = protocol::PairingDecision::Accepted}, now);
        finish_pairing_if_agreed(now);
    } else {
        observer_.on_pairing_code(crypto::pairing_code(session.handshake_hash));
    }
}

Status<Error> SessionEngine::confirm_pairing(bool accepted, TimePoint now) {
    if (state_ != SessionState::AwaitingPairing || trust_ != TrustDecision::Unknown ||
        local_decision_made_) {
        return Status<Error>::failure(Error::InvalidState);
    }
    local_decision_made_ = true;
    local_accepted_ = accepted;
    send(protocol::PairingConfirm{.decision = accepted ? protocol::PairingDecision::Accepted
                                                       : protocol::PairingDecision::Rejected},
         now);
    if (!accepted) {
        abort_session(Error::PairingRejected);
        return Status<Error>::ok();
    }
    finish_pairing_if_agreed(now);
    return Status<Error>::ok();
}

void SessionEngine::handle_pairing_confirm(const protocol::PairingConfirm& message, TimePoint now) {
    if (message.decision == protocol::PairingDecision::Rejected) {
        fail(Error::PairingRejected, false, now);
        return;
    }
    peer_accepted_ = true;
    finish_pairing_if_agreed(now);
}

void SessionEngine::finish_pairing_if_agreed(TimePoint now) {
    if (!local_accepted_ || !peer_accepted_ || state_ != SessionState::AwaitingPairing) {
        return;
    }
    if (trust_ == TrustDecision::Unknown) {
        observer_.on_paired();
        if (state_ == SessionState::Closed) {
            return;
        }
    }
    enter_negotiation(now);
}

void SessionEngine::enter_negotiation(TimePoint now) {
    // Tokens are random and never all zeros (PROTOCOL.md, SessionSetup).
    if (!random_.fill(input_token_) || !random_.fill(transfer_token_) || is_zero_id(input_token_) ||
        is_zero_id(transfer_token_)) {
        fail(Error::RandomUnavailable, false, now);
        return;
    }
    const std::uint16_t input_port = transport_.local_input_port();
    const std::uint16_t listen_port = transport_.local_listen_port();
    if (input_port == 0 || listen_port == 0) {
        fail(Error::NetworkError, false, now);
        return;
    }
    set_state(SessionState::Negotiating);
    send(offered_capabilities(), now);
    send(protocol::SessionSetup{.udp_port = input_port,
                                .listen_port = listen_port,
                                .input_token = input_token_,
                                .transfer_token = transfer_token_},
         now);
}

void SessionEngine::establish_if_ready(TimePoint /*now*/) {
    if (state_ != SessionState::Negotiating || !peer_capabilities_ || !peer_setup_) {
        return;
    }
    const protocol::Capabilities peer = *peer_capabilities_;
    set_state(SessionState::Established);
    observer_.on_established(peer);
}

void SessionEngine::handle_ping(const protocol::Ping& ping, TimePoint now) {
    send(protocol::Pong{.ping_id = ping.ping_id,
                        .ping_send_time_us = ping.send_time_us,
                        .receive_time_us = micros(now),
                        .reply_time_us = micros(now)},
         now);
}

void SessionEngine::handle_pong(const protocol::Pong& pong, TimePoint now) {
    const auto t0 = static_cast<std::int64_t>(pong.ping_send_time_us);
    const auto t1 = static_cast<std::int64_t>(pong.receive_time_us);
    const auto t2 = static_cast<std::int64_t>(pong.reply_time_us);
    const auto t3 = static_cast<std::int64_t>(micros(now));
    const std::int64_t sample = std::max<std::int64_t>((t3 - t0) - (t2 - t1), 0);
    const std::int64_t offset = ((t1 - t0) + (t2 - t3)) / 2;
    rtt_ = rtt_ ? Microseconds{((rtt_->count() * 7) + sample) / 8} : Microseconds{sample};
    clock_offsets_.add(sample, offset);
    clock_offset_us_ = clock_offsets_.offset_us().value_or(offset);
}

void SessionEngine::handle_peer_error(const protocol::PeerError& error) {
    log(LogLevel::Warning, "Peer reported error " + std::to_string(error.code));
    abort_session(reason_for_peer_error(error.code));
}

void SessionEngine::handle_bye(const protocol::Bye& bye) {
    peer_bye_ = bye.reason;
    std::optional<Error> reason = Error::ConnectionClosed;
    switch (bye.reason) {
    case protocol::ByeReason::Sleeping:
        reason = Error::PeerSleeping; // expected back
        break;
    case protocol::ByeReason::Revoked:
        reason = Error::NotTrusted;
        break;
    case protocol::ByeReason::Duplicate:
        reason.reset(); // the other connection to this peer carries on
        break;
    default:
        break;
    }
    abort_session(reason);
}

Status<Error> SessionEngine::send_message(const ControlMessage& message, TimePoint now) {
    if (!is_application(protocol::type_of(message))) {
        return Status<Error>::failure(Error::InvalidArgument);
    }
    if (state_ != SessionState::Established || closing_) {
        return Status<Error>::failure(Error::InvalidState);
    }
    if (const auto encoded = protocol::encode_control(message); !encoded) {
        return Status<Error>::failure(encoded.error());
    }
    send(message, now);
    return state_ == SessionState::Closed ? Status<Error>::failure(Error::ConnectionClosed)
                                          : Status<Error>::ok();
}

protocol::Capabilities SessionEngine::offered_capabilities() const {
    protocol::Capabilities offered = config_.capabilities;
    offered.features &= features_;
    return offered;
}

void SessionEngine::update_features(std::uint32_t features, TimePoint now) {
    if (features_ == features) {
        return;
    }
    features_ = features;
    if (state_ == SessionState::Established && !closing_) {
        send(offered_capabilities(), now);
    }
}

void SessionEngine::close(protocol::ByeReason reason, TimePoint now) {
    if (state_ == SessionState::Closed || closing_) {
        return;
    }
    if (keys_) {
        send(protocol::Bye{.reason = reason}, now);
    }
    abort_session(std::nullopt);
}

void SessionEngine::on_transport_closed(Error reason) {
    abort_session(reason);
}

// --- Input datagrams -----------------------------------------------------------------------------

bool SessionEngine::seal_and_send(const protocol::InputDatagram& datagram) {
    const auto encoded = protocol::encode_input(datagram);
    if (!encoded || !sealer_) {
        return false;
    }
    std::ranges::copy(encoded.value(), datagram_.begin());
    const auto sealed = sealer_->seal(datagram_, encoded.value().size());
    if (!sealed) {
        return false;
    }
    transport_.send_datagram(std::span(datagram_).first(sealed.value()));
    return true;
}

void SessionEngine::send_input_hello() {
    if (!peer_setup_) {
        return;
    }
    static_cast<void>(
        seal_and_send({.sequence = next_sequence_++,
                       .message = protocol::InputHello{.input_token = peer_setup_->input_token}}));
}

Status<Error> SessionEngine::send_motion(const protocol::PointerMotion& motion, TimePoint /*now*/) {
    if (state_ != SessionState::Established || closing_) {
        return Status<Error>::failure(Error::InvalidState);
    }
    if (!seal_and_send({.sequence = next_sequence_++, .message = motion})) {
        return Status<Error>::failure(Error::InvalidArgument);
    }
    return Status<Error>::ok();
}

void SessionEngine::on_datagram(std::span<std::uint8_t> datagram, TimePoint now) {
    if (state_ == SessionState::Closed || !opener_) {
        ++datagrams_rejected_;
        return;
    }
    const auto plaintext = opener_->open(datagram);
    if (!plaintext) {
        ++datagrams_rejected_;
        return;
    }
    const auto decoded = protocol::decode_input(plaintext.value());
    if (!decoded) {
        ++datagrams_rejected_;
        return;
    }
    // Authentic and never seen before (the opener drops replays): the peer is alive.
    last_datagram_ = now;
    set_responsive(true);
    const std::uint64_t sequence = decoded.value().sequence;
    std::visit(Overloaded{
                   [&](const protocol::InputHello& hello) {
                       if (hello.input_token != input_token_ || is_zero_id(input_token_)) {
                           ++datagrams_rejected_;
                           return;
                       }
                       transport_.accept_input_source(); // also follows the peer to a new address
                       input_bound_ = true;
                   },
                   [&](const protocol::PointerMotion& motion) {
                       if (state_ != SessionState::Established) {
                           ++datagrams_rejected_;
                           return;
                       }
                       // Latest wins: an older position must never move the cursor back.
                       if (last_motion_sequence_ && sequence <= *last_motion_sequence_) {
                           ++stale_motion_dropped_;
                           return;
                       }
                       last_motion_sequence_ = sequence;
                       observer_.on_motion(motion);
                   },
               },
               decoded.value().message);
}

// --- Time ----------------------------------------------------------------------------------------

TimePoint SessionEngine::last_heard() const {
    return std::max(last_received_, last_datagram_);
}

void SessionEngine::on_timer(TimePoint now) {
    if (state_ == SessionState::Closed) {
        return;
    }
    const SessionTunables& tunables = config_.tunables;
    if (!keys_) {
        if (state_ == SessionState::Handshaking &&
            now - connected_at_ >= tunables.handshake_timeout) {
            fail(Error::TimedOut, false, now);
        }
        return;
    }
    if (now - last_received_ >= tunables.session_timeout) {
        fail(Error::TimedOut, false, now);
        return;
    }
    if (state_ == SessionState::AwaitingPairing && trust_ == TrustDecision::Unknown &&
        now - keyed_at_ >= tunables.pairing_timeout) {
        fail(Error::TimedOut, false, now);
        return;
    }
    if (responsive_ && now - last_heard() >= tunables.unresponsive_after) {
        set_responsive(false);
    }
    if (now >= next_ping_) {
        send(protocol::Ping{.ping_id = next_ping_id_++, .send_time_us = micros(now)}, now);
        next_ping_ = now + tunables.ping_interval;
    }
    if (now - last_sent_ >= tunables.heartbeat_interval) {
        send(protocol::Heartbeat{}, now);
    }
    if (peer_setup_ && now >= next_input_hello_) {
        send_input_hello();
        next_input_hello_ = now + (input_bound_ ? tunables.input_keepalive_interval
                                                : tunables.input_hello_interval);
    }
    if (state_ == SessionState::Established && now >= next_stats_) {
        observer_.on_stats(stats());
        next_stats_ = now + tunables.stats_interval;
    }
}

TimePoint SessionEngine::next_wakeup() const {
    if (state_ == SessionState::Closed) {
        return TimePoint::max();
    }
    const SessionTunables& tunables = config_.tunables;
    if (!keys_) {
        return state_ == SessionState::Handshaking ? connected_at_ + tunables.handshake_timeout
                                                   : TimePoint::max();
    }
    TimePoint next = std::min({last_received_ + tunables.session_timeout, next_ping_,
                               last_sent_ + tunables.heartbeat_interval});
    if (responsive_) {
        next = std::min(next, last_heard() + tunables.unresponsive_after);
    }
    if (state_ == SessionState::AwaitingPairing && trust_ == TrustDecision::Unknown) {
        next = std::min(next, keyed_at_ + tunables.pairing_timeout);
    }
    if (peer_setup_) {
        next = std::min(next, next_input_hello_);
    }
    if (state_ == SessionState::Established) {
        next = std::min(next, next_stats_);
    }
    return next;
}

SessionStats SessionEngine::stats() const {
    return SessionStats{.rtt_us = rtt_ ? static_cast<std::uint32_t>(rtt_->count()) : 0U,
                        .clock_offset_us = clock_offset_us_,
                        .datagrams_rejected = datagrams_rejected_,
                        .stale_motion_dropped = stale_motion_dropped_};
}

std::optional<std::pair<crypto::Key, crypto::Key>>
SessionEngine::transfer_keys(std::uint32_t stream_id) const {
    if (!keys_) {
        return std::nullopt;
    }
    return std::pair{crypto::transfer_stream_key(keys_->transfer_send, stream_id),
                     crypto::transfer_stream_key(keys_->transfer_receive, stream_id)};
}

} // namespace osdrop::session
