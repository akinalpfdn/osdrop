#pragma once

// The pipe between Osdrop (the engine in the shell, or the developer tool) and OsdropInput, the
// SYSTEM process that hooks and injects input in the user's session (DEVPLAN Phase 3;
// docs/ARCHITECTURE.md, "Input pipe"). A message-mode pipe carries one message per pipe message,
// little-endian: magic u32, version u16, type u16, body length u32, body. Requests go to
// OsdropInput; reports come back, at any time while input is captured.

#include <osdrop/win/expected.hpp>
#include <osdrop/win/key_codes.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace osdrop::win::input {

inline constexpr std::uint32_t kMagic = 0x4950'444F; // "ODPI" as little-endian bytes
inline constexpr std::uint16_t kVersion = 1;
inline constexpr std::size_t kHeaderSize = 12;
inline constexpr std::size_t kMaxMessageSize = 64;
/// Virtual-desktop pixels, either sign.
inline constexpr std::int32_t kMaxCoordinate = 1 << 20;
/// One message's wheel movement in WHEEL_DELTA units (120 a notch), either sign.
inline constexpr std::int32_t kMaxScroll = 120 * 64;
/// The pipe of a session is this prefix followed by the session ID.
inline constexpr std::wstring_view kPipePrefix = L"\\\\.\\pipe\\com.akinalpfdn.osdrop.input.";

enum class Button : std::uint8_t { Left = 0, Right = 1, Middle = 2, X1 = 3, X2 = 4 };

// --- Requests, to OsdropInput ---

/// Injection is accepted from now on, until EndControl or the client goes.
struct BeginControl {
    bool operator==(const BeginControl&) const = default;
};
/// Injection stops; what was injected and is still down is released.
struct EndControl {
    bool operator==(const EndControl&) const = default;
};
/// The cursor to an absolute position, in virtual-desktop pixels.
struct Move {
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool operator==(const Move&) const = default;
};
struct Press {
    Button button = Button::Left;
    bool down = false;
    bool operator==(const Press&) const = default;
};
/// Wheel movement in WHEEL_DELTA units: vertical up and horizontal right are positive.
struct Scroll {
    std::int32_t vertical = 0;
    std::int32_t horizontal = 0;
    bool operator==(const Scroll&) const = default;
};
/// A key by position: the PC's layout turns it into a character.
struct Key {
    ScanCode scan;
    bool down = false;
    bool operator==(const Key&) const = default;
};
/// This PC's own mouse and keyboard stop acting here and are reported instead; the cursor hides,
/// parked at (park_x, park_y).
struct StartCapture {
    std::int32_t park_x = 0;
    std::int32_t park_y = 0;
    bool operator==(const StartCapture&) const = default;
};
/// The cursor shows again at (x, y), and this PC's input acts here again.
struct StopCapture {
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool operator==(const StopCapture&) const = default;
};
/// Asks for the hook's timings.
struct QueryStats {
    bool operator==(const QueryStats&) const = default;
};

using Request = std::variant<BeginControl, EndControl, Move, Press, Scroll, Key, StartCapture,
                             StopCapture, QueryStats>;

// --- Reports, from OsdropInput ---

/// Movement of this PC's mouse while captured, in pixels as the pointer settings make it.
struct CapturedMotion {
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    bool operator==(const CapturedMotion&) const = default;
};
struct CapturedPress {
    Button button = Button::Left;
    bool down = false;
    bool operator==(const CapturedPress&) const = default;
};
struct CapturedScroll {
    std::int32_t vertical = 0;
    std::int32_t horizontal = 0;
    bool operator==(const CapturedScroll&) const = default;
};
struct CapturedKey {
    ScanCode scan;
    bool down = false;
    bool operator==(const CapturedKey&) const = default;
};
/// Someone used this PC's own mouse or keyboard while another computer controls it.
struct LocalInput {
    bool operator==(const LocalInput&) const = default;
};
/// How long the hook callbacks took, over every event since OsdropInput started.
struct Stats {
    std::uint64_t events = 0;
    std::uint32_t p50_us = 0;
    std::uint32_t p99_us = 0;
    std::uint32_t max_us = 0;
    bool operator==(const Stats&) const = default;
};

enum class Refusal : std::uint8_t {
    Malformed = 1,          ///< the message could not be decoded
    UnsupportedVersion = 2, ///< another version of the protocol
    NotControlling = 3,     ///< injection outside BeginControl ... EndControl
};

/// A request was not carried out; OsdropInput logs it too.
struct Rejected {
    std::uint16_t type = 0; ///< the request's message type, 0 when it could not be read
    Refusal reason = Refusal::Malformed;
    bool operator==(const Rejected&) const = default;
};

using Report = std::variant<CapturedMotion, CapturedPress, CapturedScroll, CapturedKey, LocalInput,
                            Stats, Rejected>;

enum class DecodeError : std::uint8_t {
    Truncated,
    BadMagic,
    UnsupportedVersion,
    UnknownType,
    LengthMismatch,
    ValueOutOfRange,
};

using Bytes = std::vector<std::uint8_t>;

[[nodiscard]] Bytes encode(const Request& request);
[[nodiscard]] Bytes encode(const Report& report);
[[nodiscard]] Expected<Request, DecodeError> decode_request(std::span<const std::uint8_t> message);
[[nodiscard]] Expected<Report, DecodeError> decode_report(std::span<const std::uint8_t> message);

/// The message type of an encoded message, 0 when the header cannot be read.
[[nodiscard]] std::uint16_t type_of(std::span<const std::uint8_t> message);
[[nodiscard]] std::uint16_t type_of(const Request& request);
/// A message type's name for logs ("Move"), or "unknown".
[[nodiscard]] std::string_view type_name(std::uint16_t type);

} // namespace osdrop::win::input
