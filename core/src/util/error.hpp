#pragma once

#include <cstdint>

namespace osdrop {

/// Every failure the core reports. Values equal the osdrop_result codes of the C API and are
/// documented in docs/PROTOCOL.md ("Error codes"); new codes are only appended.
enum class Error : std::uint8_t {
    InvalidArgument = 1,
    BufferTooSmall = 2,
    Truncated = 3,
    BadMagic = 4,
    UnsupportedVersion = 5,
    UnknownType = 6,
    TrailingBytes = 7,
    ValueOutOfRange = 8,
    InvalidString = 9,
    TooLarge = 10,
    DecryptionFailed = 11,
    HandshakeOutOfOrder = 12,
    Replayed = 13,
    NonceExhausted = 14,
    InvalidKey = 15,
    RandomUnavailable = 16,
    TimedOut = 17,
    ConnectionClosed = 18,
    PeerError = 19,
    UnexpectedMessage = 20,
    NotTrusted = 21,
    PairingRejected = 22,
    PeerBusy = 23,
    InvalidState = 24,
    NetworkError = 25,
    Internal = 26,
    PeerSleeping = 27,
    NotPermitted = 28,
    GroupFull = 29,
    InAnotherGroup = 30,
    InvalidLayout = 31,
    Cancelled = 32,
    IoError = 33,
    NoSpace = 34,
    IntegrityFailed = 35,
    NotFound = 36,
};

/// The highest code this build knows; a peer's newer codes are reported as PeerError.
inline constexpr Error kLastError = Error::NotFound;

} // namespace osdrop
