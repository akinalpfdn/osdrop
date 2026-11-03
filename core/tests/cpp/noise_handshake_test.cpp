#include "crypto/noise.hpp"
#include "crypto_test_support.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

namespace {

namespace crypto = osdrop::crypto;
using crypto::HandshakeRole;
using crypto::HandshakeState;
using osdrop::Error;
using osdrop::test::from_hex;
using osdrop::test::key_from_hex;

struct Pair {
    osdrop::test::CountingRandom random;
    crypto::KeyPair initiator_static = osdrop::test::key_pair(random);
    crypto::KeyPair responder_static = osdrop::test::key_pair(random);
    HandshakeState initiator{HandshakeRole::Initiator, initiator_static, crypto::prologue(),
                             random};
    HandshakeState responder{HandshakeRole::Responder, responder_static, crypto::prologue(),
                             random};

    /// Runs all three messages; returns false at the first failure.
    bool run() {
        const auto m1 = initiator.write_message({});
        if (!m1 || !responder.read_message(m1.value())) {
            return false;
        }
        const auto m2 = responder.write_message({});
        if (!m2 || !initiator.read_message(m2.value())) {
            return false;
        }
        const auto m3 = initiator.write_message({});
        return m3 && responder.read_message(m3.value());
    }
};

TEST(NoiseHandshake, ThePrologueIsTheDocumentedText) {
    const auto prologue = crypto::prologue();
    EXPECT_EQ(std::string(prologue.begin(), prologue.end()), "Osdrop control v1");
}

TEST(NoiseHandshake, BothSidesAuthenticateAndAgreeOnKeys) {
    Pair pair;
    ASSERT_TRUE(pair.run());
    const auto initiator = pair.initiator.finish();
    const auto responder = pair.responder.finish();
    ASSERT_TRUE(initiator.has_value());
    ASSERT_TRUE(responder.has_value());
    const crypto::SessionKeys& i = initiator.value();
    const crypto::SessionKeys& r = responder.value();

    EXPECT_EQ(i.remote_static, pair.responder_static.public_key());
    EXPECT_EQ(r.remote_static, pair.initiator_static.public_key());
    EXPECT_EQ(i.handshake_hash, r.handshake_hash);
    EXPECT_EQ(i.input_send, r.input_receive);
    EXPECT_EQ(i.input_receive, r.input_send);
    EXPECT_EQ(i.transfer_send, r.transfer_receive);
    EXPECT_EQ(i.transfer_receive, r.transfer_send);
    // Every key is distinct: per direction and per channel.
    const std::set<crypto::Key> distinct{i.input_send, i.input_receive, i.transfer_send,
                                         i.transfer_receive};
    EXPECT_EQ(distinct.size(), 4U);

    auto send = i.control_send;
    auto receive = r.control_receive;
    const std::vector<std::uint8_t> payload{1, 2, 3};
    const auto ciphertext = send.encrypt_with_ad({}, payload);
    ASSERT_TRUE(ciphertext.has_value());
    const auto plaintext = receive.decrypt_with_ad({}, ciphertext.value());
    ASSERT_TRUE(plaintext.has_value());
    EXPECT_EQ(plaintext.value(), payload);
}

TEST(NoiseHandshake, PayloadsTravelWithTheHandshake) {
    Pair pair;
    const std::vector<std::uint8_t> hello{'h', 'i'};
    const auto m1 = pair.initiator.write_message(hello);
    ASSERT_TRUE(m1.has_value());
    const auto received = pair.responder.read_message(m1.value());
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received.value(), hello);
}

TEST(NoiseHandshake, MessagesOutOfOrderAreRefusedAndEndTheHandshake) {
    Pair pair;
    const auto early = pair.responder.write_message({});
    ASSERT_FALSE(early.has_value());
    EXPECT_EQ(early.error(), Error::HandshakeOutOfOrder);

    const auto m1 = pair.initiator.write_message({});
    ASSERT_TRUE(m1.has_value());
    const auto after_failure = pair.responder.read_message(m1.value());
    ASSERT_FALSE(after_failure.has_value());
    EXPECT_EQ(after_failure.error(), Error::HandshakeOutOfOrder);

    const auto too_early_finish = pair.initiator.finish();
    ASSERT_FALSE(too_early_finish.has_value());
    EXPECT_EQ(too_early_finish.error(), Error::HandshakeOutOfOrder);
}

TEST(NoiseHandshake, TamperedMessagesAreRejected) {
    Pair pair;
    const auto m1 = pair.initiator.write_message({});
    ASSERT_TRUE(m1.has_value());
    ASSERT_TRUE(pair.responder.read_message(m1.value()).has_value());
    auto m2 = pair.responder.write_message({});
    ASSERT_TRUE(m2.has_value());
    m2.value()[40] ^= 1U; // inside the encrypted static key
    const auto result = pair.initiator.read_message(m2.value());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::DecryptionFailed);
}

// Domain separation: Osplay uses the same Noise pattern with an empty prologue, so neither side
// can complete a handshake with the other.
TEST(NoiseHandshake, AHandshakeWithOsplaysPrologueFails) {
    for (const bool osplay_initiates : {true, false}) {
        SCOPED_TRACE(osplay_initiates ? "Osplay initiates" : "Osdrop initiates");
        osdrop::test::CountingRandom random;
        const auto initiator_static = osdrop::test::key_pair(random);
        const auto responder_static = osdrop::test::key_pair(random);
        const std::span<const std::uint8_t> osplay_prologue{};
        HandshakeState initiator(HandshakeRole::Initiator, initiator_static,
                                 osplay_initiates ? osplay_prologue : crypto::prologue(), random);
        HandshakeState responder(HandshakeRole::Responder, responder_static,
                                 osplay_initiates ? crypto::prologue() : osplay_prologue, random);

        const auto m1 = initiator.write_message({});
        ASSERT_TRUE(m1.has_value());
        ASSERT_TRUE(responder.read_message(m1.value()).has_value());
        const auto m2 = responder.write_message({});
        ASSERT_TRUE(m2.has_value());
        const auto result = initiator.read_message(m2.value());
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), Error::DecryptionFailed);
        EXPECT_FALSE(initiator.write_message({}).has_value()) << "the handshake stays failed";
    }
}

TEST(NoiseHandshake, LowOrderEphemeralKeysAreRejected) {
    Pair pair;
    const std::vector<std::uint8_t> zero_ephemeral(crypto::kKeySize, 0);
    ASSERT_TRUE(pair.responder.read_message(zero_ephemeral).has_value());
    const auto m2 = pair.responder.write_message({});
    ASSERT_FALSE(m2.has_value());
    EXPECT_EQ(m2.error(), Error::InvalidKey);
}

TEST(NoiseHandshake, TruncatedAndOversizedMessagesAreRejected) {
    Pair pair;
    const auto short_message = pair.responder.read_message(std::vector<std::uint8_t>(31));
    ASSERT_FALSE(short_message.has_value());
    EXPECT_EQ(short_message.error(), Error::Truncated);

    Pair other;
    const auto huge =
        other.responder.read_message(std::vector<std::uint8_t>(crypto::kMaxNoiseMessageSize + 1));
    ASSERT_FALSE(huge.has_value());
    EXPECT_EQ(huge.error(), Error::TooLarge);
}

TEST(NoiseTransport, TamperedReplayedAndReorderedMessagesAreRejected) {
    Pair pair;
    ASSERT_TRUE(pair.run());
    auto send = pair.initiator.finish().value().control_send;
    auto receive = pair.responder.finish().value().control_receive;
    const std::vector<std::uint8_t> first{1};
    const std::vector<std::uint8_t> second{2};
    const auto c1 = send.encrypt_with_ad({}, first);
    const auto c2 = send.encrypt_with_ad({}, second);
    ASSERT_TRUE(c1.has_value());
    ASSERT_TRUE(c2.has_value());

    auto tampered = c1.value();
    tampered[0] ^= 1U;
    EXPECT_EQ(receive.decrypt_with_ad({}, tampered).error(), Error::DecryptionFailed);
    EXPECT_EQ(receive.nonce(), 0U) << "a forged message must not advance the nonce";

    EXPECT_EQ(receive.decrypt_with_ad({}, c2.value()).error(), Error::DecryptionFailed)
        << "reordered";
    ASSERT_TRUE(receive.decrypt_with_ad({}, c1.value()).has_value());
    EXPECT_EQ(receive.decrypt_with_ad({}, c1.value()).error(), Error::DecryptionFailed)
        << "replayed";
    EXPECT_TRUE(receive.decrypt_with_ad({}, c2.value()).has_value());
}

// Expected keys computed with Python's hmac and hashlib.blake2b from the derivations in
// docs/PROTOCOL.md ("Session keys").
TEST(SessionKeys, MatchTheDocumentedDerivation) {
    crypto::Hash chaining_key{};
    for (std::size_t i = 0; i < chaining_key.size(); ++i) {
        chaining_key.at(i) = static_cast<std::uint8_t>(0x40 + i);
    }
    const auto split_first =
        key_from_hex("fbae6305753b22641f6bc7924aaae82065a324f57985244d97c623b209ffc86e");
    const auto split_second =
        key_from_hex("22c794d3a500f0e2db08a64e17a4bc4c563216d0a8aea052f02beb380aa4bbcd");
    const auto input_first =
        key_from_hex("a589f0b737cc982b777bb09938ccb71972b39e9c20b3622243df241300caf0c6");
    const auto input_second =
        key_from_hex("6c65958752971a680ab82388a5724b1d79a9f63bb49209f94dc2f45312e9599e");
    const auto transfer_first =
        key_from_hex("3f24e3531f420f01b80e8768382548837288f23edfb4bb1b67a3d8b76312a34a");
    const auto transfer_second =
        key_from_hex("682c04edd1fdcc99a466107cfd908611dab7421e77305c70ab79e7c8d4114234");

    auto initiator = crypto::derive_session_keys(chaining_key, {}, {}, HandshakeRole::Initiator);
    auto responder = crypto::derive_session_keys(chaining_key, {}, {}, HandshakeRole::Responder);
    EXPECT_EQ(initiator.input_send, input_first);
    EXPECT_EQ(initiator.input_receive, input_second);
    EXPECT_EQ(initiator.transfer_send, transfer_first);
    EXPECT_EQ(initiator.transfer_receive, transfer_second);
    EXPECT_EQ(responder.input_send, input_second);
    EXPECT_EQ(responder.transfer_send, transfer_second);

    // The control keys are inside cipher states: they must encrypt as the expected keys do.
    const std::vector<std::uint8_t> frame{'O', 'D'};
    crypto::CipherState expected_first(split_first);
    crypto::CipherState expected_second(split_second);
    EXPECT_EQ(initiator.control_send.encrypt_with_ad({}, frame).value(),
              expected_first.encrypt_with_ad({}, frame).value());
    EXPECT_EQ(responder.control_send.encrypt_with_ad({}, frame).value(),
              expected_second.encrypt_with_ad({}, frame).value());
}

TEST(SessionKeys, TransferStreamKeysMatchTheDocumentedDerivation) {
    const auto base =
        key_from_hex("a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf");
    EXPECT_EQ(crypto::transfer_stream_key(base, 1),
              key_from_hex("2f0f3f83ed5a7d3d92bd061ad429bfad6c21826e53b22e828fa7c62b56a5bfdd"));
    EXPECT_EQ(crypto::transfer_stream_key(base, 2),
              key_from_hex("67afd833c1e2f4b982b27e0d32da1e2cb4789628009a2380aadd396230157db5"));
    EXPECT_EQ(crypto::transfer_stream_key(base, 0xFFFF'FFFF),
              key_from_hex("08225d5fc877bac0f6fe5404bab17a72ad64eb34fa6b1a6eb3cdc6a8dcca8897"));
}

TEST(SessionKeys, EachStreamAndDirectionHasItsOwnKey) {
    Pair pair;
    ASSERT_TRUE(pair.run());
    const auto keys = pair.initiator.finish().value();
    std::set<crypto::Key> seen;
    for (std::uint32_t stream_id = 1; stream_id <= 64; ++stream_id) {
        seen.insert(crypto::transfer_stream_key(keys.transfer_send, stream_id));
        seen.insert(crypto::transfer_stream_key(keys.transfer_receive, stream_id));
    }
    EXPECT_EQ(seen.size(), 128U);
}

TEST(PairingCode, BothSidesDeriveTheSameSixDigitCode) {
    Pair pair;
    ASSERT_TRUE(pair.run());
    const auto initiator_code = crypto::pairing_code(pair.initiator.handshake_hash());
    EXPECT_EQ(initiator_code, crypto::pairing_code(pair.responder.handshake_hash()));
    EXPECT_LT(initiator_code, 1'000'000U);
}

TEST(PairingCode, AnyChangeToTheTranscriptChangesTheCode) {
    Pair reference;
    ASSERT_TRUE(reference.run());
    const auto reference_code = crypto::pairing_code(reference.initiator.handshake_hash());

    // Same keys and randomness, one payload byte different.
    Pair changed;
    const auto m1 = changed.initiator.write_message(std::vector<std::uint8_t>{1});
    ASSERT_TRUE(m1.has_value());
    ASSERT_TRUE(changed.responder.read_message(m1.value()).has_value());
    const auto m2 = changed.responder.write_message({});
    ASSERT_TRUE(m2.has_value());
    ASSERT_TRUE(changed.initiator.read_message(m2.value()).has_value());
    const auto m3 = changed.initiator.write_message({});
    ASSERT_TRUE(m3.has_value());
    ASSERT_TRUE(changed.responder.read_message(m3.value()).has_value());
    EXPECT_NE(crypto::pairing_code(changed.initiator.handshake_hash()), reference_code);

    const crypto::Hash& hash = reference.initiator.handshake_hash();
    for (std::size_t bit = 0; bit < hash.size() * 8; bit += 61) {
        crypto::Hash variant = hash;
        variant.at(bit / 8) ^= static_cast<std::uint8_t>(1U << (bit % 8));
        EXPECT_NE(crypto::pairing_code(variant), reference_code) << "bit " << bit;
    }
}

// Expected values computed with Python's hashlib.blake2b (docs/PROTOCOL.md defines the
// derivations). The hash is the cacophony vector's handshake hash.
TEST(PairingCode, MatchesTheDocumentedDerivation) {
    const auto hash = from_hex("8cf47d7b3cb5804c0109d48e8bcdbee2cbb65687d8ea2c92994ca361fb86151a"
                               "d93627b98936cbb32de56e8abb21def3925011ac3e35db9cbeea73ab9a4392c2");
    crypto::Hash handshake_hash{};
    std::copy(hash.begin(), hash.end(), handshake_hash.begin());
    EXPECT_EQ(crypto::pairing_code(handshake_hash), 333'649U);
}

TEST(DeviceId, MatchesTheDocumentedDerivation) {
    const auto id = crypto::device_id(
        key_from_hex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"));
    EXPECT_EQ(std::vector<std::uint8_t>(id.begin(), id.end()),
              from_hex("7a91869e4c114964206972403b0a3d81"));
}

TEST(KeyPair, RejectsAnAllZeroSecretAndReportsRandomFailure) {
    EXPECT_EQ(crypto::KeyPair::from_secret(crypto::Key{}).error(), Error::InvalidKey);
    osdrop::test::ScriptedRandom empty;
    EXPECT_EQ(crypto::KeyPair::generate(empty).error(), Error::RandomUnavailable);
}

} // namespace
