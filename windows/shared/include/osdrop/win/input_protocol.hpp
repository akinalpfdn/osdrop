#pragma once

// The pipe between Osdrop (the engine in the shell, or the developer tool) and OsdropInput, the
// SYSTEM process that hooks and injects input in the user's session (DEVPLAN Phases 3 and 11;
// docs/ARCHITECTURE.md, "Input pipe"). A message-mode pipe carries one message per pipe message,
// little-endian: magic u32, version u16, type u16, body length u32, body. Requests go to
// OsdropInput; reports come back at any time while this PC's input is watched or captured.

#include <osdrop/win/chords.hpp>
#include <osdrop/win/expected.hpp>
#include <osdrop/win/key_codes.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace osdrop::win::input {

inline constexpr std::uint32_t kMagic = 0x4950'444F; // "ODPI" as little-endian bytes
inline constexpr std::uint16_t kVersion = 3;
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
/// The cursor to an absolute position, in virtual-desktop pixels. Move, Press, Scroll and Key
/// inject: they are accepted while the client controls this PC (BeginControl) or captures its
/// input (StartCapture), which releases keys here and sets Caps Lock.
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
/// parked at (park_x, park_y). Capture lasts while Heartbeats come; when they stop, or the user
/// presses the return chord, OsdropInput ends it by itself (Released) with the cursor at
/// (home_x, home_y), where it left.
struct StartCapture {
    std::int32_t park_x = 0;
    std::int32_t park_y = 0;
    std::int32_t home_x = 0;
    std::int32_t home_y = 0;
    bool operator==(const StartCapture&) const = default;
};
/// The cursor shows again at (x, y), and this PC's input acts here again. Also after Released,
/// which ended the capture here first.
struct StopCapture {
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool operator==(const StopCapture&) const = default;
};
/// Asks for the hook's timings.
struct QueryStats {
    bool operator==(const QueryStats&) const = default;
};
/// While `on`, this PC's own input is reported as it acts here (WatchedMotion and the Physical
/// reports), and the hotkeys are Osdrop's.
struct Watch {
    bool on = false;
    bool operator==(const Watch&) const = default;
};
/// The capture goes on; send one at least every kHeartbeatInterval while capturing.
struct Heartbeat {
    bool operator==(const Heartbeat&) const = default;
};
/// The user's chords (valid_pair); until a client sends them, and after it goes, the defaults.
struct SetChords {
    Chord return_home = kReturnHomeChord;
    Chord lock_input = kLockInputChord;
    bool operator==(const SetChords&) const = default;
};
/// Before EndControl: the injected left button stays down after it, because a drag on this PC
/// waits at the edge where the controller left for its outcome (DEVPLAN Phase 14). ReleaseDrag,
/// or the client's end, releases it.
struct HoldDrag {
    bool operator==(const HoldDrag&) const = default;
};
/// Releases the left button where the cursor is, in any state: a drag on this PC that waits for
/// its outcome ends there.
struct ReleaseDrag {
    bool operator==(const ReleaseDrag&) const = default;
};
/// A drag that came home with this PC's own hand starts here: the left button goes down where the
/// cursor is, in any state, but only while this PC's own left button is held, so its release ends
/// the drag (Rejected NotHeld otherwise). The hand pressed the button during the capture, where the
/// press went to the other computer and not to this PC (DEVPLAN Phase 14). The cursor stays where
/// the hand moved it: the engine's window that starts the drag covers the desktop.
struct PressForDrag {
    bool operator==(const PressForDrag&) const = default;
};

using Request =
    std::variant<BeginControl, EndControl, Move, Press, Scroll, Key, StartCapture, StopCapture,
                 QueryStats, Watch, Heartbeat, SetChords, HoldDrag, ReleaseDrag, PressForDrag>;

/// How often a capturing client sends Heartbeat, and how long OsdropInput waits for one.
inline constexpr std::chrono::milliseconds kHeartbeatInterval{250};
inline constexpr std::chrono::milliseconds kHeartbeatTimeout{1000};

// --- Reports, from OsdropInput ---

/// Movement of this PC's mouse while captured, in pixels as the pointer settings make it.
/// `ticks`: QueryPerformanceCounter when the hook saw it (the earliest, when moves add up).
struct CapturedMotion {
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    std::uint64_t ticks = 0;
    bool operator==(const CapturedMotion&) const = default;
};
/// Movement of this PC's mouse while watched: (x, y) where Windows takes the cursor, before it
/// keeps it on a monitor, so a push against an outer edge still moves by (dx, dy).
struct WatchedMotion {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    std::uint64_t ticks = 0;
    bool operator==(const WatchedMotion&) const = default;
};
/// A button, wheel or key of this PC, while watched or captured.
struct PhysicalPress {
    Button button = Button::Left;
    bool down = false;
    bool operator==(const PhysicalPress&) const = default;
};
struct PhysicalScroll {
    std::int32_t vertical = 0;
    std::int32_t horizontal = 0;
    bool operator==(const PhysicalScroll&) const = default;
};
struct PhysicalKey {
    ScanCode scan;
    bool down = false;
    bool operator==(const PhysicalKey&) const = default;
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
    NotControlling = 3,     ///< injection while the client neither controls nor captures
    NotHeld = 4,            ///< PressForDrag while this PC's own left button is up
};

/// A request was not carried out; OsdropInput logs it too.
struct Rejected {
    std::uint16_t type = 0; ///< the request's message type, 0 when it could not be read
    Refusal reason = Refusal::Malformed;
    bool operator==(const Rejected&) const = default;
};

/// Osdrop's own key chords; the key that completes one never reaches an app.
enum class Hotkey : std::uint8_t {
    ReturnHome = 1, ///< the cursor comes back to this PC; during capture: Released instead
    LockInput = 2,  ///< the cursor stays on the computer it is on, or crosses again
};
struct HotkeyPressed {
    Hotkey hotkey = Hotkey::ReturnHome;
    bool operator==(const HotkeyPressed&) const = default;
};

enum class ReleaseReason : std::uint8_t {
    ReturnChord = 1, ///< the user pressed the return chord
    Watchdog = 2,    ///< no Heartbeat for kHeartbeatTimeout
};
/// OsdropInput ended the capture by itself: the cursor is back at the StartCapture home and this
/// PC's input acts here. The client still sends StopCapture once the cursor is home.
struct Released {
    ReleaseReason reason = ReleaseReason::ReturnChord;
    bool operator==(const Released&) const = default;
};

using Report = std::variant<CapturedMotion, PhysicalPress, PhysicalScroll, PhysicalKey, LocalInput,
                            Stats, Rejected, WatchedMotion, HotkeyPressed, Released>;

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
