#pragma once

// Noise_XX_25519_ChaChaPoly_BLAKE2b (Noise Protocol Framework, revision 34), the only pattern
// Osdrop uses, and the derivations on top of it (docs/PROTOCOL.md, "Encryption"). Names follow the
// specification's sections 5 (processing rules) and 7.5 (XX).

#include "crypto/primitives.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace osdrop::crypto {

using Bytes = std::vector<std::uint8_t>;

inline constexpr std::size_t kMaxNoiseMessageSize = 65535;

/// The prologue of every Osdrop handshake: the ASCII bytes of "Osdrop control v1".
[[nodiscard]] std::span<const std::uint8_t> prologue();

/// A static or ephemeral X25519 key pair. The secret is wiped when the object goes away.
class KeyPair {
public:
    /// Fails with InvalidKey for an all-zero secret (uninitialized storage, not a real key).
    [[nodiscard]] static Expected<KeyPair, Error> from_secret(const Key& secret);
    [[nodiscard]] static Expected<KeyPair, Error> generate(RandomSource& random);

    KeyPair(const KeyPair& other) = default;
    KeyPair& operator=(const KeyPair& other) = default;
    KeyPair(KeyPair&& other) noexcept = default;
    KeyPair& operator=(KeyPair&& other) noexcept = default;
    ~KeyPair();

    [[nodiscard]] const Key& secret() const { return secret_; }
    [[nodiscard]] const Key& public_key() const { return public_key_; }

private:
    KeyPair(const Key& secret, const Key& public_key) : secret_(secret), public_key_(public_key) {}

    Key secret_{};
    Key public_key_{};
};

/// Noise CipherState: a key and a 64-bit nonce that increments after every message.
class CipherState {
public:
    CipherState() = default;
    explicit CipherState(const Key& key) : key_(key) {}
    CipherState(const CipherState& other) = default;
    CipherState& operator=(const CipherState& other) = default;
    CipherState(CipherState&& other) noexcept = default;
    CipherState& operator=(CipherState&& other) noexcept = default;
    ~CipherState();

    [[nodiscard]] bool has_key() const { return key_.has_value(); }
    [[nodiscard]] std::uint64_t nonce() const { return nonce_; }

    /// Without a key, returns the plaintext unchanged (Noise EncryptWithAd).
    [[nodiscard]] Expected<Bytes, Error> encrypt_with_ad(std::span<const std::uint8_t> ad,
                                                         std::span<const std::uint8_t> plaintext);
    /// The nonce advances only when authentication succeeds.
    [[nodiscard]] Expected<Bytes, Error> decrypt_with_ad(std::span<const std::uint8_t> ad,
                                                         std::span<const std::uint8_t> ciphertext);

private:
    std::optional<Key> key_;
    std::uint64_t nonce_ = 0;
};

enum class HandshakeRole : std::uint8_t { Initiator, Responder };

/// Keys and identities that come out of a completed handshake. "Send" keys protect what this side
/// sends, "receive" keys what the peer sends. Raw keys are wiped when the object goes away.
struct SessionKeys {
    SessionKeys() = default;
    SessionKeys(const SessionKeys& other) = default;
    SessionKeys& operator=(const SessionKeys& other) = default;
    SessionKeys(SessionKeys&& other) noexcept = default;
    SessionKeys& operator=(SessionKeys&& other) noexcept = default;
    ~SessionKeys();

    CipherState control_send; // control channel (Noise Split)
    CipherState control_receive;
    Key input_send{}; // input datagrams
    Key input_receive{};
    Key transfer_send{}; // transfer base keys; see transfer_stream_key()
    Key transfer_receive{};
    Hash handshake_hash{};
    Key remote_static{};
};

/// The session keys a final chaining key and handshake hash give `role` (docs/PROTOCOL.md,
/// "Session keys"). HandshakeState::finish() uses it; it is separate so the derivation can be
/// checked against independently computed values.
[[nodiscard]] SessionKeys derive_session_keys(const Hash& chaining_key, const Hash& handshake_hash,
                                              const Key& remote_static, HandshakeRole role);

/// One direction's key for a transfer stream: keyed BLAKE2b-256 of the stream ID under that
/// direction's transfer base key.
[[nodiscard]] Key transfer_stream_key(const Key& base, std::uint32_t stream_id);

/// XX handshake for one side. Message order (initiator): write, read, write; (responder): read,
/// write, read. Any failure ends the handshake for good, as Noise requires.
class HandshakeState {
public:
    HandshakeState(HandshakeRole role, KeyPair static_keys, std::span<const std::uint8_t> prologue,
                   RandomSource& random);

    /// The next handshake message, carrying `payload`.
    [[nodiscard]] Expected<Bytes, Error> write_message(std::span<const std::uint8_t> payload);
    /// Processes the peer's next handshake message and returns its payload.
    [[nodiscard]] Expected<Bytes, Error> read_message(std::span<const std::uint8_t> message);

    [[nodiscard]] bool is_complete() const { return step_ == 3 && !failed_; }
    [[nodiscard]] const Hash& handshake_hash() const { return h_; }

    /// Splits into session keys. Only valid once is_complete() is true.
    [[nodiscard]] Expected<SessionKeys, Error> finish() const;

private:
    void mix_hash(std::span<const std::uint8_t> data);
    void mix_key(std::span<const std::uint8_t> input_key_material);
    [[nodiscard]] Expected<Bytes, Error> encrypt_and_hash(std::span<const std::uint8_t> plaintext);
    [[nodiscard]] Expected<Bytes, Error> decrypt_and_hash(std::span<const std::uint8_t> ciphertext);
    [[nodiscard]] Status<Error> mix_dh(const KeyPair& local, const Key& remote);
    [[nodiscard]] Status<Error> mix_dh_ephemeral(const Key& remote);
    [[nodiscard]] std::size_t minimum_message_size() const;
    [[nodiscard]] static Hash initial_hash();
    [[nodiscard]] bool writes_next() const;
    Error fail(Error error);

    HandshakeRole role_;
    KeyPair static_keys_;
    RandomSource* random_; // never null; owned by the caller and must outlive the handshake
    std::optional<KeyPair> ephemeral_;
    Key remote_ephemeral_{};
    Key remote_static_{};
    Hash h_{};
    Hash ck_{};
    CipherState cipher_;
    int step_ = 0; // handshake messages processed so far
    bool failed_ = false;
};

/// Six-digit code both users compare when pairing (docs/PROTOCOL.md, "Pairing code").
[[nodiscard]] std::uint32_t pairing_code(const Hash& handshake_hash);

/// Device ID advertised over Bonjour: the first 16 bytes of BLAKE2b-256 over the public key.
[[nodiscard]] std::array<std::uint8_t, 16> device_id(const Key& public_key);

} // namespace osdrop::crypto
