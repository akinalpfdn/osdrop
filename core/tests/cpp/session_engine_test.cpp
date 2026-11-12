// SessionEngine rules the mesh tests do not reach: what may travel in plaintext, version and error
// handling, timeouts and pairing. Two engines talk through an in-memory pipe with a fake clock.
#include "crypto_test_support.hpp"
#include "protocol/control_messages.hpp"
#include "session/control_framer.hpp"
#include "session/session_engine.hpp"

#include <gtest/gtest.h>

#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace {

namespace crypto = osdrop::crypto;
namespace protocol = osdrop::protocol;
namespace session = osdrop::session;
using osdrop::Error;
using osdrop::TimePoint;
using session::SessionState;
using std::chrono::milliseconds;

class Side final : public session::SessionObserver, public session::SessionTransport {
public:
    Side(session::SessionRole role, std::uint64_t seed)
        : random(seed), identity(osdrop::test::key_pair(random)),
          engine(role, config(), identity, random, *this, *this) {}

    static session::SessionConfig config() {
        return session::SessionConfig{
            .platform = protocol::Platform::Windows,
            .device_name = "Test",
            .group_id = {},
            .capabilities = protocol::Capabilities{.features = protocol::kKnownFeatures,
                                                   .max_transfer_streams = 1},
            .tunables = {}};
    }

    // SessionObserver
    void on_state(SessionState state, std::optional<Error> reason) override {
        if (state == SessionState::Closed) {
            close_reason = reason;
        }
    }
    void on_peer_hello(const protocol::Hello& /*hello*/) override {}
    session::TrustVerdict on_trust_query(const crypto::Key& /*key*/) override {
        return {.decision = trust, .refusal = refusal, .features = features};
    }
    void on_pairing_code(std::uint32_t code) override { codes.push_back(code); }
    void on_paired() override {}
    void on_established(const protocol::Capabilities& peer) override {
        established = true;
        peer_capabilities = peer;
    }
    void on_capabilities(const protocol::Capabilities& peer) override { peer_capabilities = peer; }
    void on_responsive(bool responsive) override { responsiveness.push_back(responsive); }
    void on_message(const protocol::ControlMessage& /*message*/) override {}
    void on_motion(const protocol::PointerMotion& /*motion*/) override {}
    void on_stats(const session::SessionStats& /*stats*/) override {}
    void on_log(session::LogLevel /*level*/, std::string_view /*message*/) override {}

    // SessionTransport
    void send_control(std::span<const std::uint8_t> frame) override {
        outbox.emplace_back(frame.begin(), frame.end());
    }
    void send_datagram(std::span<const std::uint8_t> datagram) override {
        datagrams.emplace_back(datagram.begin(), datagram.end());
    }
    void set_input_destination_port(std::uint16_t /*port*/) override {}
    void accept_input_source() override { ++input_sources_accepted; }
    std::uint16_t local_input_port() override { return 40'001; }
    std::uint16_t local_listen_port() const override { return 49'152; }
    void close() override { closed = true; }

    [[nodiscard]] bool is_closed() const { return engine.state() == SessionState::Closed; }

    osdrop::test::SeededRandom random;
    crypto::KeyPair identity;
    session::SessionEngine engine;
    session::TrustDecision trust = session::TrustDecision::Trusted;
    Error refusal = Error::NotTrusted;
    std::uint32_t features = protocol::kKnownFeatures;
    std::optional<protocol::Capabilities> peer_capabilities;
    std::optional<Error> close_reason;
    std::vector<std::uint32_t> codes;
    bool established = false;
    std::vector<bool> responsiveness;
    std::deque<std::vector<std::uint8_t>> outbox;
    std::deque<std::vector<std::uint8_t>> datagrams;
    int input_sources_accepted = 0;
    bool closed = false;
};

std::vector<std::uint8_t> frame(const protocol::ControlMessage& message) {
    std::vector<std::uint8_t> bytes;
    session::write_frame(protocol::encode_control(message).value(), bytes);
    return bytes;
}

/// The plaintext message in a frame the engine wrote before the handshake.
protocol::ControlMessage unframe(const std::vector<std::uint8_t>& bytes) {
    return protocol::decode_control(std::span(bytes).subspan(session::kFrameHeaderSize)).value();
}

struct Pair {
    /// Different seeds give different identities and ephemeral keys.
    explicit Pair(std::uint64_t seed = 1)
        : dialer(session::SessionRole::Dialer, seed),
          listener(session::SessionRole::Listener, seed + 1) {}

    Side dialer;
    Side listener;
    TimePoint now{std::chrono::seconds(100)};
    /// Control bytes wait while true, as behind a lost segment TCP resends; datagrams still go.
    bool control_stalled = false;

    /// Delivers queued frames and datagrams both ways until nothing moves.
    void pump() {
        for (bool moved = true; moved;) {
            moved = false;
            for (auto [from, to] : {std::pair(&dialer, &listener), std::pair(&listener, &dialer)}) {
                while (!control_stalled && !from->outbox.empty()) {
                    to->engine.on_control_bytes(from->outbox.front(), now);
                    from->outbox.pop_front();
                    moved = true;
                }
                while (!from->datagrams.empty()) {
                    to->engine.on_datagram(from->datagrams.front(), now);
                    from->datagrams.pop_front();
                    moved = true;
                }
            }
        }
    }

    void connect() {
        dialer.engine.on_connected(now);
        listener.engine.on_connected(now);
        pump();
    }

    void advance(milliseconds duration) {
        const TimePoint end = now + duration;
        while (now < end) {
            now += milliseconds(1);
            dialer.engine.on_timer(now);
            listener.engine.on_timer(now);
            pump();
        }
    }
};

TEST(SessionEngine, TwoSidesAuthenticateEachOtherAndEstablishASession) {
    Pair pair;
    pair.connect();
    EXPECT_EQ(pair.dialer.engine.state(), SessionState::Established);
    EXPECT_EQ(pair.listener.engine.state(), SessionState::Established);
    EXPECT_TRUE(pair.dialer.established);
    EXPECT_EQ(pair.dialer.engine.peer_static_key(), pair.listener.identity.public_key());
    EXPECT_EQ(pair.listener.engine.peer_static_key(), pair.dialer.identity.public_key());
    ASSERT_TRUE(pair.dialer.engine.peer_setup().has_value());
    EXPECT_EQ(pair.dialer.engine.peer_setup()->listen_port, 49'152);
    EXPECT_EQ(pair.dialer.input_sources_accepted, 1) << "the listener's InputHello arrived";
    EXPECT_EQ(pair.listener.input_sources_accepted, 1);
    EXPECT_EQ(pair.dialer.engine.local_transfer_token(),
              pair.listener.engine.peer_setup()->transfer_token);
}

TEST(SessionEngine, ANewerMajorVersionIsReportedToThePeer) {
    Side listener(session::SessionRole::Listener, 2);
    const TimePoint now{std::chrono::seconds(100)};
    listener.engine.on_connected(now);
    auto hello =
        frame(protocol::Hello{.platform = protocol::Platform::MacOS, .device_name = "New"});
    hello[session::kFrameHeaderSize + 2] = 2; // protocol version 2
    listener.engine.on_control_bytes(hello, now);
    EXPECT_EQ(listener.close_reason, Error::UnsupportedVersion);
    ASSERT_EQ(listener.outbox.size(), 2U); // its own Hello, then the Error
    EXPECT_EQ(unframe(listener.outbox.back()),
              protocol::ControlMessage(protocol::PeerError{
                  .code = static_cast<std::uint16_t>(Error::UnsupportedVersion)}));
}

TEST(SessionEngine, OnlyHelloHandshakeAndErrorTravelInPlaintext) {
    Side listener(session::SessionRole::Listener, 2);
    const TimePoint now{std::chrono::seconds(100)};
    listener.engine.on_connected(now);
    listener.engine.on_control_bytes(
        frame(protocol::DragResult{.drag_id = 1, .outcome = protocol::DragOutcome::Dropped}), now);
    EXPECT_EQ(listener.close_reason, Error::UnexpectedMessage);
    EXPECT_EQ(unframe(listener.outbox.back()),
              protocol::ControlMessage(protocol::PeerError{
                  .code = static_cast<std::uint16_t>(Error::UnexpectedMessage)}));
}

TEST(SessionEngine, AHandshakeThatStallsTimesOut) {
    Side dialer(session::SessionRole::Dialer, 1);
    TimePoint now{std::chrono::seconds(100)};
    dialer.engine.on_connected(now);
    EXPECT_EQ(dialer.engine.next_wakeup(), now + std::chrono::seconds(10));
    now += std::chrono::seconds(10);
    dialer.engine.on_timer(now);
    EXPECT_EQ(dialer.close_reason, Error::TimedOut);
}

TEST(SessionEngine, BlockedPeersAreToldWhy) {
    Pair pair;
    pair.listener.trust = session::TrustDecision::Blocked;
    pair.connect();
    EXPECT_EQ(pair.listener.close_reason, Error::NotTrusted);
    EXPECT_EQ(pair.dialer.close_reason, Error::NotTrusted);
}

TEST(SessionEngine, ARefusalCarriesItsReasonToThePeer) {
    Pair pair;
    pair.listener.trust = session::TrustDecision::Blocked;
    pair.listener.refusal = Error::InAnotherGroup;
    pair.connect();
    EXPECT_EQ(pair.listener.close_reason, Error::InAnotherGroup);
    EXPECT_EQ(pair.dialer.close_reason, Error::InAnotherGroup);
}

TEST(SessionEngine, AnnouncedFeaturesFollowPermissionsAndCanChangeLater) {
    Pair pair;
    pair.listener.features = protocol::kFeatureTakesControl | protocol::kFeatureDragSource;
    pair.connect();
    ASSERT_TRUE(pair.dialer.peer_capabilities.has_value());
    EXPECT_EQ(pair.dialer.peer_capabilities->features,
              protocol::kFeatureTakesControl | protocol::kFeatureDragSource);
    EXPECT_EQ(pair.listener.peer_capabilities->features, protocol::kKnownFeatures);

    pair.listener.engine.update_features(protocol::kFeatureClipboard, pair.now);
    pair.pump();
    EXPECT_EQ(pair.dialer.peer_capabilities->features, protocol::kFeatureClipboard);
}

TEST(SessionEngine, AFirstPairingWaitsForBothUsersAndTimesOutWithoutAnswer) {
    Pair pair;
    pair.dialer.trust = session::TrustDecision::Unknown;
    pair.listener.trust = session::TrustDecision::Unknown;
    pair.connect();
    ASSERT_EQ(pair.dialer.codes.size(), 1U);
    EXPECT_EQ(pair.dialer.codes, pair.listener.codes);
    EXPECT_EQ(pair.dialer.engine.state(), SessionState::AwaitingPairing);
    EXPECT_EQ(pair.dialer.engine
                  .send_message(
                      protocol::DragResult{.drag_id = 1, .outcome = protocol::DragOutcome::Dropped},
                      pair.now)
                  .error(),
              Error::InvalidState);

    pair.advance(std::chrono::seconds(119)); // heartbeats keep the session alive meanwhile
    EXPECT_EQ(pair.dialer.engine.state(), SessionState::AwaitingPairing);
    pair.advance(std::chrono::seconds(2));
    EXPECT_EQ(pair.dialer.close_reason, Error::TimedOut);
}

TEST(SessionEngine, ConfirmingAPairingIsOnlyPossibleOnce) {
    Pair pair;
    pair.dialer.trust = session::TrustDecision::Unknown;
    pair.listener.trust = session::TrustDecision::Unknown;
    pair.connect();
    ASSERT_TRUE(pair.dialer.engine.confirm_pairing(true, pair.now).has_value());
    EXPECT_EQ(pair.dialer.engine.confirm_pairing(true, pair.now).error(), Error::InvalidState);
    pair.pump();
    ASSERT_TRUE(pair.listener.engine.confirm_pairing(true, pair.now).has_value());
    pair.pump();
    EXPECT_EQ(pair.dialer.engine.state(), SessionState::Established);
    EXPECT_EQ(pair.listener.engine.state(), SessionState::Established);
}

TEST(SessionEngine, ByeReasonsBecomeCloseReasons) {
    const std::vector<std::pair<protocol::ByeReason, std::optional<Error>>> cases{
        {protocol::ByeReason::UserDisconnected, Error::ConnectionClosed},
        {protocol::ByeReason::Sleeping, Error::PeerSleeping},
        {protocol::ByeReason::Revoked, Error::NotTrusted},
        {protocol::ByeReason::Duplicate, std::nullopt},
    };
    for (const auto& [reason, expected] : cases) {
        SCOPED_TRACE(static_cast<int>(reason));
        Pair pair;
        pair.connect();
        pair.listener.engine.close(reason, pair.now);
        pair.pump();
        EXPECT_TRUE(pair.dialer.is_closed());
        EXPECT_EQ(pair.dialer.close_reason, expected);
        EXPECT_EQ(pair.dialer.engine.peer_bye(), reason);
        EXPECT_EQ(pair.listener.close_reason, std::nullopt) << "closing is not an error";
    }
}

TEST(SessionEngine, AnIdleSessionKeepsItsPeerResponsive) {
    Pair pair;
    pair.connect();
    pair.advance(std::chrono::seconds(5));
    EXPECT_TRUE(pair.dialer.responsiveness.empty());
    EXPECT_TRUE(pair.listener.responsiveness.empty());
    EXPECT_LE(pair.dialer.engine.next_wakeup(), pair.now + milliseconds(250))
        << "a heartbeat is due within the interval";
}

TEST(SessionEngine, DatagramsKeepThePeerResponsiveWhileTheControlConnectionStalls) {
    Pair pair;
    pair.connect();
    pair.control_stalled = true;
    pair.advance(std::chrono::seconds(2));
    EXPECT_TRUE(pair.dialer.responsiveness.empty());
    EXPECT_TRUE(pair.listener.responsiveness.empty());
    pair.control_stalled = false;
    pair.advance(milliseconds(10));
    EXPECT_EQ(pair.dialer.engine.state(), SessionState::Established);
}

TEST(SessionEngine, DatagramsAloneDoNotKeepASessionPastItsTimeout) {
    Pair pair;
    pair.connect();
    pair.control_stalled = true;
    pair.advance(std::chrono::seconds(11));
    EXPECT_TRUE(pair.dialer.is_closed());
    EXPECT_EQ(pair.dialer.close_reason, Error::TimedOut);
}

TEST(SessionEngine, DatagramsFromAnotherSessionAreRejected) {
    Pair first(1);
    Pair second(11);
    first.connect();
    second.connect();
    const protocol::PointerMotion motion{.control_id = 1, .x = 5, .y = 5, .capture_time_us = 1};
    ASSERT_TRUE(first.dialer.engine.send_motion(motion, first.now).has_value());
    ASSERT_EQ(first.dialer.datagrams.size(), 1U);
    second.listener.engine.on_datagram(first.dialer.datagrams.front(), second.now);
    EXPECT_EQ(second.listener.engine.stats().datagrams_rejected, 1U);
}

} // namespace
