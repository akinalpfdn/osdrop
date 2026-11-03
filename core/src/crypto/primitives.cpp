#include "crypto/primitives.hpp"

#include <monocypher.h>

#include <algorithm>

namespace osdrop::crypto {
namespace {

std::array<std::uint8_t, 12> noise_nonce(std::uint64_t counter) {
    std::array<std::uint8_t, 12> nonce{};
    for (std::size_t i = 0; i < 8; ++i) {
        nonce.at(4 + i) = static_cast<std::uint8_t>(counter >> (8U * i));
    }
    return nonce;
}

} // namespace

void wipe(std::span<std::uint8_t> secret) {
    crypto_wipe(secret.data(), secret.size());
}

Key x25519_public_key(const Key& secret) {
    Key public_key{};
    crypto_x25519_public_key(public_key.data(), secret.data());
    return public_key;
}

Expected<Key, Error> x25519(const Key& secret, const Key& peer_public) {
    Key shared{};
    crypto_x25519(shared.data(), secret.data(), peer_public.data());
    // Constant-time all-zero check: a zero result means the peer sent a low-order point.
    std::uint8_t accumulated = 0;
    for (const std::uint8_t byte : shared) {
        accumulated = static_cast<std::uint8_t>(accumulated | byte);
    }
    if (accumulated == 0) {
        return Expected<Key, Error>::failure(Error::InvalidKey);
    }
    return shared;
}

void blake2b(std::span<std::uint8_t> out, ByteParts parts) {
    crypto_blake2b_ctx context{};
    crypto_blake2b_init(&context, out.size());
    for (const auto part : parts) {
        crypto_blake2b_update(&context, part.data(), part.size());
    }
    crypto_blake2b_final(&context, out.data());
}

Hash blake2b(ByteParts parts) {
    Hash hash{};
    blake2b(hash, parts);
    return hash;
}

void blake2b_keyed(std::span<std::uint8_t> out, std::span<const std::uint8_t> key,
                   ByteParts parts) {
    crypto_blake2b_ctx context{};
    crypto_blake2b_keyed_init(&context, out.size(), key.data(), key.size());
    for (const auto part : parts) {
        crypto_blake2b_update(&context, part.data(), part.size());
    }
    crypto_blake2b_final(&context, out.data());
}

Hash hmac_blake2b(std::span<const std::uint8_t> key, ByteParts data) {
    std::array<std::uint8_t, kHashBlockSize> padded_key{};
    if (key.size() > kHashBlockSize) {
        blake2b(std::span(padded_key).first(kHashSize), {key});
    } else {
        std::ranges::copy(key, padded_key.begin());
    }

    std::array<std::uint8_t, kHashBlockSize> inner_pad{};
    std::array<std::uint8_t, kHashBlockSize> outer_pad{};
    for (std::size_t i = 0; i < kHashBlockSize; ++i) {
        inner_pad.at(i) = static_cast<std::uint8_t>(padded_key.at(i) ^ 0x36U);
        outer_pad.at(i) = static_cast<std::uint8_t>(padded_key.at(i) ^ 0x5CU);
    }

    crypto_blake2b_ctx context{};
    crypto_blake2b_init(&context, kHashSize);
    crypto_blake2b_update(&context, inner_pad.data(), inner_pad.size());
    for (const auto part : data) {
        crypto_blake2b_update(&context, part.data(), part.size());
    }
    Hash inner{};
    crypto_blake2b_final(&context, inner.data());

    const Hash result = blake2b({outer_pad, inner});
    wipe(padded_key);
    wipe(inner_pad);
    wipe(outer_pad);
    wipe(inner);
    return result;
}

void aead_encrypt(const Key& key, std::uint64_t nonce, std::span<const std::uint8_t> ad,
                  std::span<const std::uint8_t> plaintext, std::span<std::uint8_t> out) {
    const auto nonce_bytes = noise_nonce(nonce);
    crypto_aead_ctx context{};
    // A fresh context per message: its first write is exactly RFC 8439 (later writes would
    // ratchet).
    crypto_aead_init_ietf(&context, key.data(), nonce_bytes.data());
    crypto_aead_write(&context, out.data(), out.subspan(plaintext.size(), kTagSize).data(),
                      ad.data(), ad.size(), plaintext.data(), plaintext.size());
    crypto_wipe(&context, sizeof(context));
}

bool aead_decrypt(const Key& key, std::uint64_t nonce, std::span<const std::uint8_t> ad,
                  std::span<const std::uint8_t> ciphertext, std::span<std::uint8_t> out) {
    if (ciphertext.size() < kTagSize) {
        return false;
    }
    const std::size_t length = ciphertext.size() - kTagSize;
    const auto nonce_bytes = noise_nonce(nonce);
    crypto_aead_ctx context{};
    crypto_aead_init_ietf(&context, key.data(), nonce_bytes.data());
    const int result =
        crypto_aead_read(&context, out.data(), ciphertext.subspan(length, kTagSize).data(),
                         ad.data(), ad.size(), ciphertext.data(), length);
    crypto_wipe(&context, sizeof(context));
    return result == 0;
}

} // namespace osdrop::crypto
