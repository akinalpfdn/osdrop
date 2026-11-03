#pragma once

// Thin wrappers over Monocypher for the primitives of Noise_XX_25519_ChaChaPoly_BLAKE2b.

#include "util/error.hpp"
#include "util/expected.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>

namespace osdrop::crypto {

inline constexpr std::size_t kKeySize = 32;
inline constexpr std::size_t kHashSize = 64; // BLAKE2b-512, Noise HASHLEN
inline constexpr std::size_t kHashBlockSize = 128;
inline constexpr std::size_t kTagSize = 16;

using Key = std::array<std::uint8_t, kKeySize>;
using Hash = std::array<std::uint8_t, kHashSize>;
using ByteParts = std::initializer_list<std::span<const std::uint8_t>>;

/// Cryptographically secure random bytes, supplied by the platform (core has no OS access).
class RandomSource {
public:
    virtual ~RandomSource() = default;
    [[nodiscard]] virtual bool fill(std::span<std::uint8_t> out) = 0;
};

/// Overwrites secrets so they do not linger in freed memory.
void wipe(std::span<std::uint8_t> secret);

[[nodiscard]] Key x25519_public_key(const Key& secret);

/// Shared secret; fails with InvalidKey for an all-zero result (low-order peer key).
[[nodiscard]] Expected<Key, Error> x25519(const Key& secret, const Key& peer_public);

/// BLAKE2b over the concatenation of `parts`, with an output of `out.size()` bytes (1-64).
void blake2b(std::span<std::uint8_t> out, ByteParts parts);
[[nodiscard]] Hash blake2b(ByteParts parts);

/// Keyed BLAKE2b (RFC 7693) with a key of 1-64 bytes, otherwise as blake2b().
void blake2b_keyed(std::span<std::uint8_t> out, std::span<const std::uint8_t> key, ByteParts parts);

/// HMAC (RFC 2104) with BLAKE2b-512, as Noise defines it.
[[nodiscard]] Hash hmac_blake2b(std::span<const std::uint8_t> key, ByteParts data);

/// ChaCha20-Poly1305 (RFC 8439) with the Noise nonce: 32 zero bits, then the counter little-endian.
/// `out` receives the ciphertext followed by the 16-byte tag; it must be plaintext.size() + 16
/// long and may alias the plaintext.
void aead_encrypt(const Key& key, std::uint64_t nonce, std::span<const std::uint8_t> ad,
                  std::span<const std::uint8_t> plaintext, std::span<std::uint8_t> out);

/// Verifies and decrypts ciphertext||tag into `out` (ciphertext.size() - 16 bytes, may alias).
/// Returns false, leaving `out` unspecified, if authentication fails.
[[nodiscard]] bool aead_decrypt(const Key& key, std::uint64_t nonce,
                                std::span<const std::uint8_t> ad,
                                std::span<const std::uint8_t> ciphertext,
                                std::span<std::uint8_t> out);

} // namespace osdrop::crypto
