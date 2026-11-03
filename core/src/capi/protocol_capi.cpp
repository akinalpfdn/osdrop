#include "protocol/control_messages.hpp"
#include "protocol/input_messages.hpp"
#include "protocol/transfer_messages.hpp"

#include <osdrop/osdrop.h>

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace protocol = osdrop::protocol;
using osdrop::Error;

static_assert(static_cast<int>(Error::InvalidArgument) == OSDROP_ERROR_INVALID_ARGUMENT);
static_assert(static_cast<int>(Error::BufferTooSmall) == OSDROP_ERROR_BUFFER_TOO_SMALL);
static_assert(static_cast<int>(Error::Truncated) == OSDROP_ERROR_TRUNCATED);
static_assert(static_cast<int>(Error::BadMagic) == OSDROP_ERROR_BAD_MAGIC);
static_assert(static_cast<int>(Error::UnsupportedVersion) == OSDROP_ERROR_UNSUPPORTED_VERSION);
static_assert(static_cast<int>(Error::UnknownType) == OSDROP_ERROR_UNKNOWN_TYPE);
static_assert(static_cast<int>(Error::TrailingBytes) == OSDROP_ERROR_TRAILING_BYTES);
static_assert(static_cast<int>(Error::ValueOutOfRange) == OSDROP_ERROR_VALUE_OUT_OF_RANGE);
static_assert(static_cast<int>(Error::InvalidString) == OSDROP_ERROR_INVALID_STRING);
static_assert(static_cast<int>(Error::TooLarge) == OSDROP_ERROR_TOO_LARGE);
static_assert(static_cast<int>(Error::DecryptionFailed) == OSDROP_ERROR_DECRYPTION_FAILED);
static_assert(static_cast<int>(Error::HandshakeOutOfOrder) == OSDROP_ERROR_HANDSHAKE_OUT_OF_ORDER);
static_assert(static_cast<int>(Error::Replayed) == OSDROP_ERROR_REPLAYED);
static_assert(static_cast<int>(Error::NonceExhausted) == OSDROP_ERROR_NONCE_EXHAUSTED);
static_assert(static_cast<int>(Error::InvalidKey) == OSDROP_ERROR_INVALID_KEY);
static_assert(static_cast<int>(Error::RandomUnavailable) == OSDROP_ERROR_RANDOM_UNAVAILABLE);
static_assert(static_cast<int>(Error::TimedOut) == OSDROP_ERROR_TIMED_OUT);
static_assert(static_cast<int>(Error::ConnectionClosed) == OSDROP_ERROR_CONNECTION_CLOSED);
static_assert(static_cast<int>(Error::PeerError) == OSDROP_ERROR_PEER_ERROR);
static_assert(static_cast<int>(Error::UnexpectedMessage) == OSDROP_ERROR_UNEXPECTED_MESSAGE);
static_assert(static_cast<int>(Error::NotTrusted) == OSDROP_ERROR_NOT_TRUSTED);
static_assert(static_cast<int>(Error::PairingRejected) == OSDROP_ERROR_PAIRING_REJECTED);
static_assert(static_cast<int>(Error::PeerBusy) == OSDROP_ERROR_PEER_BUSY);
static_assert(static_cast<int>(Error::InvalidState) == OSDROP_ERROR_INVALID_STATE);
static_assert(static_cast<int>(Error::NetworkError) == OSDROP_ERROR_NETWORK);
static_assert(static_cast<int>(Error::Internal) == OSDROP_ERROR_INTERNAL);
static_assert(static_cast<int>(Error::PeerSleeping) == OSDROP_ERROR_PEER_SLEEPING);
static_assert(static_cast<int>(Error::NotPermitted) == OSDROP_ERROR_NOT_PERMITTED);
static_assert(static_cast<int>(Error::GroupFull) == OSDROP_ERROR_GROUP_FULL);
static_assert(static_cast<int>(Error::InAnotherGroup) == OSDROP_ERROR_IN_ANOTHER_GROUP);
static_assert(OSDROP_PROTOCOL_VERSION == protocol::kVersion);
static_assert(OSDROP_PROTOCOL_MINOR == protocol::kMinorVersion);
static_assert(OSDROP_MAX_CONTROL_MESSAGE_SIZE == protocol::kMaxControlMessageSize);
static_assert(OSDROP_MAX_TRANSFER_MESSAGE_SIZE == protocol::kMaxTransferMessageSize);
static_assert(OSDROP_MAX_INPUT_PLAINTEXT_SIZE == protocol::kMaxInputPlaintextSize);

osdrop_result to_result(Error error) {
    return static_cast<osdrop_result>(error);
}

bool valid_input(osdrop_wire_kind kind, const std::uint8_t* data, std::size_t length) {
    const bool known_kind = kind == OSDROP_WIRE_CONTROL_MESSAGE ||
                            kind == OSDROP_WIRE_INPUT_DATAGRAM ||
                            kind == OSDROP_WIRE_TRANSFER_MESSAGE;
    return known_kind && (data != nullptr || length == 0);
}

std::span<const std::uint8_t> as_span(const std::uint8_t* data, std::size_t length) {
    return length == 0 ? std::span<const std::uint8_t>{}
                       : std::span<const std::uint8_t>(data, length);
}

/// Outcome of decoding: a result code and, on success, the PROTOCOL.md type value.
struct Decoded {
    osdrop_result result = OSDROP_OK;
    std::uint8_t type = 0;
};

/// Re-encodes the decoded value into `encoded` when it is not null.
template <typename DecodeResult, typename Encode>
Decoded finish(const DecodeResult& decoded, std::uint8_t type, Encode encode,
               std::vector<std::uint8_t>* encoded) {
    if (encoded != nullptr) {
        auto bytes = encode(decoded.value());
        if (!bytes) {
            return {.result = to_result(bytes.error())};
        }
        *encoded = std::move(bytes.value());
    }
    return {.type = type};
}

Decoded decode(osdrop_wire_kind kind, std::span<const std::uint8_t> input,
               std::vector<std::uint8_t>* encoded) {
    switch (kind) {
    case OSDROP_WIRE_CONTROL_MESSAGE: {
        const auto message = protocol::decode_control(input);
        if (!message) {
            return {.result = to_result(message.error())};
        }
        return finish(message, static_cast<std::uint8_t>(protocol::type_of(message.value())),
                      protocol::encode_control, encoded);
    }
    case OSDROP_WIRE_INPUT_DATAGRAM: {
        const auto datagram = protocol::decode_input(input);
        if (!datagram) {
            return {.result = to_result(datagram.error())};
        }
        return finish(datagram,
                      static_cast<std::uint8_t>(protocol::type_of(datagram.value().message)),
                      protocol::encode_input, encoded);
    }
    case OSDROP_WIRE_TRANSFER_MESSAGE: {
        const auto message = protocol::decode_transfer(input);
        if (!message) {
            return {.result = to_result(message.error())};
        }
        return finish(message, static_cast<std::uint8_t>(protocol::type_of(message.value())),
                      protocol::encode_transfer, encoded);
    }
    case OSDROP_WIRE_KIND_MAX_ENUM:
        break;
    }
    return {.result = OSDROP_ERROR_INVALID_ARGUMENT};
}

} // namespace

extern "C" {

osdrop_result osdrop_protocol_validate(osdrop_wire_kind kind, const uint8_t* data, size_t length,
                                       uint8_t* out_type) {
    if (!valid_input(kind, data, length) || out_type == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const Decoded decoded = decode(kind, as_span(data, length), nullptr);
    if (decoded.result == OSDROP_OK) {
        *out_type = decoded.type;
    }
    return decoded.result;
}

osdrop_result osdrop_protocol_reencode(osdrop_wire_kind kind, const uint8_t* data, size_t length,
                                       uint8_t* out, size_t capacity, size_t* out_length) {
    if (!valid_input(kind, data, length) || (out == nullptr && capacity > 0) ||
        out_length == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    std::vector<std::uint8_t> encoded;
    const Decoded decoded = decode(kind, as_span(data, length), &encoded);
    if (decoded.result != OSDROP_OK) {
        return decoded.result;
    }
    if (encoded.size() > capacity) {
        return OSDROP_ERROR_BUFFER_TOO_SMALL;
    }
    std::ranges::copy(encoded, out);
    *out_length = encoded.size();
    return OSDROP_OK;
}

} // extern "C"
