#include "crypto/noise.hpp"

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace osdrop::crypto {
namespace {

constexpr std::string_view kProtocolName = "Noise_XX_25519_ChaChaPoly_BLAKE2b";
// Osdrop's prologue and the labels of its derivations on top of Noise (docs/PROTOCOL.md,
// "Encryption").
constexpr std::string_view kPrologue = "Osdrop control v1";
constexpr std::string_view kInputKeysLabel = "Osdrop input keys v1";
constexpr std::string_view kTransferKeysLabel = "Osdrop transfer keys v1";
constexpr std::string_view kTransferStreamLabel = "Osdrop transfer stream v1";
constexpr std::string_view kPairingCodeLabel = "Osdrop pairing code v1";
constexpr std::uint32_t kPairingCodeModulus = 1'000'000;
constexpr std::size_t kEncryptedKeySize = kKeySize + kTagSize;

std::span<const std::uint8_t> bytes_of(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

struct HkdfOutputs {
    Hash first{};
    Hash second{};
};

/// Noise HKDF with two outputs.
HkdfOutputs hkdf(const Hash& chaining_key, std::span<const std::uint8_t> input_key_material) {
    Hash temp_key = hmac_blake2b(chaining_key, {input_key_material});
    constexpr std::array<std::uint8_t, 1> kOne{0x01};
    constexpr std::array<std::uint8_t, 1> kTwo{0x02};
    HkdfOutputs out;
    out.first = hmac_blake2b(temp_key, {kOne});
    out.second = hmac_blake2b(temp_key, {out.first, kTwo});
    wipe(temp_key);
    return out;
}

Key truncated(const Hash& hash) {
    Key key{};
    std::copy_n(hash.begin(), kKeySize, key.begin());
    return key;
}

bool is_all_zero(const Key& key) {
    std::uint8_t accumulated = 0;
    for (const std::uint8_t byte : key) {
        accumulated = static_cast<std::uint8_t>(accumulated | byte);
    }
    return accumulated == 0;
}

} // namespace

std::span<const std::uint8_t> prologue() {
    return bytes_of(kPrologue);
}

// --- KeyPair -------------------------------------------------------------------------------------

Expected<KeyPair, Error> KeyPair::from_secret(const Key& secret) {
    if (is_all_zero(secret)) {
        return Expected<KeyPair, Error>::failure(Error::InvalidKey);
    }
    return KeyPair(secret, x25519_public_key(secret));
}

Expected<KeyPair, Error> KeyPair::generate(RandomSource& random) {
    Key secret{};
    if (!random.fill(secret)) {
        return Expected<KeyPair, Error>::failure(Error::RandomUnavailable);
    }
    auto pair = from_secret(secret);
    wipe(secret);
    return pair;
}

KeyPair::~KeyPair() {
    wipe(secret_);
}

// --- CipherState ---------------------------------------------------------------------------------

CipherState::~CipherState() {
    if (key_) {
        wipe(*key_);
    }
}

Expected<Bytes, Error> CipherState::encrypt_with_ad(std::span<const std::uint8_t> ad,
                                                    std::span<const std::uint8_t> plaintext) {
    if (!key_) {
        return Bytes(plaintext.begin(), plaintext.end());
    }
    // 2^64-1 is reserved by Noise; a session never gets close, but must not wrap around.
    if (nonce_ == std::numeric_limits<std::uint64_t>::max()) {
        return Expected<Bytes, Error>::failure(Error::NonceExhausted);
    }
    Bytes out(plaintext.size() + kTagSize);
    aead_encrypt(*key_, nonce_, ad, plaintext, out);
    ++nonce_;
    return out;
}

Expected<Bytes, Error> CipherState::decrypt_with_ad(std::span<const std::uint8_t> ad,
                                                    std::span<const std::uint8_t> ciphertext) {
    if (!key_) {
        return Bytes(ciphertext.begin(), ciphertext.end());
    }
    if (nonce_ == std::numeric_limits<std::uint64_t>::max()) {
        return Expected<Bytes, Error>::failure(Error::NonceExhausted);
    }
    if (ciphertext.size() < kTagSize) {
        return Expected<Bytes, Error>::failure(Error::DecryptionFailed);
    }
    Bytes out(ciphertext.size() - kTagSize);
    if (!aead_decrypt(*key_, nonce_, ad, ciphertext, out)) {
        wipe(out);
        return Expected<Bytes, Error>::failure(Error::DecryptionFailed);
    }
    ++nonce_;
    return out;
}

// --- HandshakeState ------------------------------------------------------------------------------

HandshakeState::HandshakeState(HandshakeRole role, KeyPair static_keys,
                               std::span<const std::uint8_t> prologue, RandomSource& random)
    : role_(role), static_keys_(std::move(static_keys)), random_(&random), h_(initial_hash()),
      ck_(initial_hash()) {
    mix_hash(prologue);
}

Hash HandshakeState::initial_hash() {
    // InitializeSymmetric: the name fits in HASHLEN, so h is the name padded with zeros.
    static_assert(kProtocolName.size() <= kHashSize);
    Hash hash{};
    std::ranges::copy(bytes_of(kProtocolName), hash.begin());
    return hash;
}

void HandshakeState::mix_hash(std::span<const std::uint8_t> data) {
    h_ = blake2b({h_, data});
}

void HandshakeState::mix_key(std::span<const std::uint8_t> input_key_material) {
    HkdfOutputs out = hkdf(ck_, input_key_material);
    ck_ = out.first;
    cipher_ = CipherState(truncated(out.second));
    wipe(out.first);
    wipe(out.second);
}

Expected<Bytes, Error> HandshakeState::encrypt_and_hash(std::span<const std::uint8_t> plaintext) {
    auto ciphertext = cipher_.encrypt_with_ad(h_, plaintext);
    if (ciphertext) {
        mix_hash(ciphertext.value());
    }
    return ciphertext;
}

Expected<Bytes, Error> HandshakeState::decrypt_and_hash(std::span<const std::uint8_t> ciphertext) {
    auto plaintext = cipher_.decrypt_with_ad(h_, ciphertext);
    if (plaintext) {
        mix_hash(ciphertext);
    }
    return plaintext;
}

Status<Error> HandshakeState::mix_dh(const KeyPair& local, const Key& remote) {
    auto shared = x25519(local.secret(), remote);
    if (!shared) {
        return Status<Error>::failure(shared.error());
    }
    mix_key(shared.value());
    wipe(shared.value());
    return Status<Error>::ok();
}

bool HandshakeState::writes_next() const {
    // XX: -> e | <- e, ee, s, es | -> s, se
    const bool initiator_turn = step_ % 2 == 0;
    return initiator_turn == (role_ == HandshakeRole::Initiator);
}

Status<Error> HandshakeState::mix_dh_ephemeral(const Key& remote) {
    if (!ephemeral_) {
        return Status<Error>::failure(Error::HandshakeOutOfOrder);
    }
    return mix_dh(*ephemeral_, remote);
}

std::size_t HandshakeState::minimum_message_size() const {
    switch (step_) {
    case 0:
        return kKeySize; // e
    case 1:
        return kKeySize + kEncryptedKeySize + kTagSize; // e, encrypted s, payload tag
    default:
        return kEncryptedKeySize + kTagSize; // encrypted s, payload tag
    }
}

Error HandshakeState::fail(Error error) {
    failed_ = true;
    ephemeral_.reset();
    return error;
}

Expected<Bytes, Error> HandshakeState::write_message(std::span<const std::uint8_t> payload) {
    using Result = Expected<Bytes, Error>;
    if (failed_ || step_ >= 3 || !writes_next()) {
        return Result::failure(fail(Error::HandshakeOutOfOrder));
    }

    Bytes message;
    const auto append = [&](std::span<const std::uint8_t> part) {
        message.insert(message.end(), part.begin(), part.end());
    };

    if (step_ == 0 || step_ == 1) { // token e
        auto ephemeral = KeyPair::generate(*random_);
        if (!ephemeral) {
            return Result::failure(fail(ephemeral.error()));
        }
        ephemeral_ = std::move(ephemeral.value());
        append(ephemeral_->public_key());
        mix_hash(ephemeral_->public_key());
    }
    if (step_ == 1) { // tokens ee, s, es (responder)
        if (const auto ee = mix_dh_ephemeral(remote_ephemeral_); !ee) {
            return Result::failure(fail(ee.error()));
        }
        const auto encrypted_static = encrypt_and_hash(static_keys_.public_key());
        if (!encrypted_static) {
            return Result::failure(fail(encrypted_static.error()));
        }
        append(encrypted_static.value());
        if (const auto es = mix_dh(static_keys_, remote_ephemeral_); !es) {
            return Result::failure(fail(es.error()));
        }
    }
    if (step_ == 2) { // tokens s, se (initiator)
        const auto encrypted_static = encrypt_and_hash(static_keys_.public_key());
        if (!encrypted_static) {
            return Result::failure(fail(encrypted_static.error()));
        }
        append(encrypted_static.value());
        if (const auto se = mix_dh(static_keys_, remote_ephemeral_); !se) {
            return Result::failure(fail(se.error()));
        }
    }

    const auto encrypted_payload = encrypt_and_hash(payload);
    if (!encrypted_payload) {
        return Result::failure(fail(encrypted_payload.error()));
    }
    append(encrypted_payload.value());
    if (message.size() > kMaxNoiseMessageSize) {
        return Result::failure(fail(Error::TooLarge));
    }
    ++step_;
    return message;
}

Expected<Bytes, Error> HandshakeState::read_message(std::span<const std::uint8_t> message) {
    using Result = Expected<Bytes, Error>;
    if (failed_ || step_ >= 3 || writes_next()) {
        return Result::failure(fail(Error::HandshakeOutOfOrder));
    }
    if (message.size() > kMaxNoiseMessageSize) {
        return Result::failure(fail(Error::TooLarge));
    }

    std::size_t offset = 0;
    const auto take = [&](std::size_t count) {
        const auto part = message.subspan(offset, count);
        offset += count;
        return part;
    };
    if (message.size() < minimum_message_size()) {
        return Result::failure(fail(Error::Truncated));
    }

    if (step_ == 0 || step_ == 1) { // token e
        std::ranges::copy(take(kKeySize), remote_ephemeral_.begin());
        mix_hash(remote_ephemeral_);
    }
    if (step_ == 1) { // tokens ee, s, es (initiator reading)
        if (const auto ee = mix_dh_ephemeral(remote_ephemeral_); !ee) {
            return Result::failure(fail(ee.error()));
        }
        const auto remote_static = decrypt_and_hash(take(kEncryptedKeySize));
        if (!remote_static) {
            return Result::failure(fail(remote_static.error()));
        }
        std::ranges::copy(remote_static.value(), remote_static_.begin());
        if (const auto es = mix_dh_ephemeral(remote_static_); !es) {
            return Result::failure(fail(es.error()));
        }
    }
    if (step_ == 2) { // tokens s, se (responder reading)
        const auto remote_static = decrypt_and_hash(take(kEncryptedKeySize));
        if (!remote_static) {
            return Result::failure(fail(remote_static.error()));
        }
        std::ranges::copy(remote_static.value(), remote_static_.begin());
        if (const auto se = mix_dh_ephemeral(remote_static_); !se) {
            return Result::failure(fail(se.error()));
        }
    }

    auto payload = decrypt_and_hash(take(message.size() - offset));
    if (!payload) {
        return Result::failure(fail(payload.error()));
    }
    ++step_;
    if (step_ == 3) {
        ephemeral_.reset(); // forward secrecy: the ephemeral secret is no longer needed
    }
    return payload;
}

Expected<SessionKeys, Error> HandshakeState::finish() const {
    if (!is_complete()) {
        return Expected<SessionKeys, Error>::failure(Error::HandshakeOutOfOrder);
    }
    return derive_session_keys(ck_, h_, remote_static_, role_);
}

// --- Session keys --------------------------------------------------------------------------------

SessionKeys::~SessionKeys() {
    wipe(input_send);
    wipe(input_receive);
    wipe(transfer_send);
    wipe(transfer_receive);
}

SessionKeys derive_session_keys(const Hash& chaining_key, const Hash& handshake_hash,
                                const Key& remote_static, HandshakeRole role) {
    HkdfOutputs control = hkdf(chaining_key, {});
    HkdfOutputs input = hkdf(chaining_key, bytes_of(kInputKeysLabel));
    HkdfOutputs transfer = hkdf(chaining_key, bytes_of(kTransferKeysLabel));
    // First outputs protect initiator -> responder, second outputs responder -> initiator.
    const bool initiator = role == HandshakeRole::Initiator;
    const auto send = [initiator](const HkdfOutputs& out) {
        return truncated(initiator ? out.first : out.second);
    };
    const auto receive = [initiator](const HkdfOutputs& out) {
        return truncated(initiator ? out.second : out.first);
    };

    SessionKeys keys;
    keys.control_send = CipherState(send(control));
    keys.control_receive = CipherState(receive(control));
    keys.input_send = send(input);
    keys.input_receive = receive(input);
    keys.transfer_send = send(transfer);
    keys.transfer_receive = receive(transfer);
    keys.handshake_hash = handshake_hash;
    keys.remote_static = remote_static;

    for (HkdfOutputs* out : {&control, &input, &transfer}) {
        wipe(out->first);
        wipe(out->second);
    }
    return keys;
}

Key transfer_stream_key(const Key& base, std::uint32_t stream_id) {
    std::array<std::uint8_t, 4> id{};
    for (std::size_t i = 0; i < id.size(); ++i) {
        id.at(i) = static_cast<std::uint8_t>(stream_id >> (8U * i));
    }
    Key key{};
    blake2b_keyed(key, base, {bytes_of(kTransferStreamLabel), id});
    return key;
}

// --- Identity helpers ----------------------------------------------------------------------------

std::uint32_t pairing_code(const Hash& handshake_hash) {
    std::array<std::uint8_t, 32> digest{};
    blake2b(digest, {bytes_of(kPairingCodeLabel), handshake_hash});
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= std::uint64_t{digest.at(i)} << (8U * i);
    }
    // 2^64 is not a multiple of 10^6, but the bias is below 10^-13.
    return static_cast<std::uint32_t>(value % kPairingCodeModulus);
}

std::array<std::uint8_t, 16> device_id(const Key& public_key) {
    std::array<std::uint8_t, 32> digest{};
    blake2b(digest, {public_key});
    std::array<std::uint8_t, 16> id{};
    std::copy_n(digest.begin(), id.size(), id.begin());
    return id;
}

} // namespace osdrop::crypto
