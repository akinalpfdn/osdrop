#include "crypto/primitives.hpp"
#include "crypto_test_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace {

namespace crypto = osdrop::crypto;
using osdrop::test::from_hex;
using osdrop::test::key_from_hex;

std::vector<std::uint8_t> bytes(const std::string& text) {
    return {text.begin(), text.end()};
}

std::vector<std::uint8_t> counting(std::size_t size) {
    std::vector<std::uint8_t> out(size);
    for (std::size_t i = 0; i < size; ++i) {
        out[i] = static_cast<std::uint8_t>(i);
    }
    return out;
}

TEST(CryptoPrimitives, X25519MatchesRfc7748) {
    const auto alice =
        key_from_hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const auto bob =
        key_from_hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    const auto alice_public = crypto::x25519_public_key(alice);
    const auto bob_public = crypto::x25519_public_key(bob);
    EXPECT_EQ(alice_public,
              key_from_hex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"));
    EXPECT_EQ(bob_public,
              key_from_hex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"));

    const auto alice_shared = crypto::x25519(alice, bob_public);
    const auto bob_shared = crypto::x25519(bob, alice_public);
    ASSERT_TRUE(alice_shared.has_value());
    ASSERT_TRUE(bob_shared.has_value());
    EXPECT_EQ(alice_shared.value(), bob_shared.value());
    EXPECT_EQ(alice_shared.value(),
              key_from_hex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742"));
}

TEST(CryptoPrimitives, X25519RejectsLowOrderPeerKeys) {
    const auto secret =
        key_from_hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const auto shared = crypto::x25519(secret, crypto::Key{}); // the zero point has order 4
    ASSERT_FALSE(shared.has_value());
    EXPECT_EQ(shared.error(), osdrop::Error::InvalidKey);
}

TEST(CryptoPrimitives, Blake2bMatchesRfc7693) {
    const auto abc = bytes("abc");
    const auto hash = crypto::blake2b({abc});
    EXPECT_EQ(std::vector<std::uint8_t>(hash.begin(), hash.end()),
              from_hex("ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
                       "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"));
}

TEST(CryptoPrimitives, KeyedBlake2bMatchesTheReferenceVectors) {
    // BLAKE2 reference known-answer test: key 00..3f, empty input.
    std::array<std::uint8_t, crypto::kHashSize> kat{};
    crypto::blake2b_keyed(kat, counting(64), {});
    EXPECT_EQ(std::vector<std::uint8_t>(kat.begin(), kat.end()),
              from_hex("10ebb67700b1868efb4417987acf4690ae9d972fb7a590c2f02871799aaa4786"
                       "b5e996e8f0f4eb981fc214b005f42d2ff4233499391653df7aefcbc13fc51568"));

    // Python hashlib.blake2b(b"abc", key=bytes(range(32)), digest_size=32), split input.
    crypto::Key short_hash{};
    const auto a = bytes("a");
    const auto bc = bytes("bc");
    crypto::blake2b_keyed(short_hash, counting(32), {a, bc});
    EXPECT_EQ(short_hash,
              key_from_hex("d63a32d3e44738d7907f964316c241adaba0abfeabc32349677578a15a203f7f"));
}

// Expected values computed with Python's hmac and hashlib.blake2b, an independent implementation.
TEST(CryptoPrimitives, HmacBlake2bMatchesPythonStdlib) {
    const auto message = bytes("Osdrop");
    const auto mac = crypto::hmac_blake2b(counting(64), {message});
    EXPECT_EQ(std::vector<std::uint8_t>(mac.begin(), mac.end()),
              from_hex("7f08b825473c9e2cb7535e34828c062bb74259ddab72b9abba3aa3cb937fc3ed"
                       "0680f9fa52e9940c5cb10ff498070b46bed099fba8b54bf3148b4656bd935915"));

    const auto long_mac = crypto::hmac_blake2b(counting(200), {message});
    EXPECT_EQ(std::vector<std::uint8_t>(long_mac.begin(), long_mac.end()),
              from_hex("1c599a2dfb1bfcdb6505d58d2c8ad3aa8446d8ffcd574a54c77f3d290ddc27f2"
                       "256b84f160bbf9afa38f02bf68c83f6e20f0932e68d86d62bd5b718438f5af69"));
}

TEST(CryptoPrimitives, AeadRoundTripsAndDetectsTampering) {
    const auto key =
        key_from_hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    const auto ad = bytes("header");
    const auto plaintext = bytes("frame payload");
    std::vector<std::uint8_t> sealed(plaintext.size() + crypto::kTagSize);
    crypto::aead_encrypt(key, 7, ad, plaintext, sealed);

    std::vector<std::uint8_t> opened(plaintext.size());
    EXPECT_TRUE(crypto::aead_decrypt(key, 7, ad, sealed, opened));
    EXPECT_EQ(opened, plaintext);

    EXPECT_FALSE(crypto::aead_decrypt(key, 8, ad, sealed, opened)) << "wrong nonce";
    auto other_ad = ad;
    other_ad[0] ^= 1U;
    EXPECT_FALSE(crypto::aead_decrypt(key, 7, other_ad, sealed, opened)) << "wrong associated data";
    auto tampered = sealed;
    tampered[3] ^= 1U;
    EXPECT_FALSE(crypto::aead_decrypt(key, 7, ad, tampered, opened)) << "tampered ciphertext";
    EXPECT_FALSE(crypto::aead_decrypt(key, 7, ad, std::span(sealed).first(10), opened))
        << "shorter than a tag";
}

} // namespace
