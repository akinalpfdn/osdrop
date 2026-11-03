#pragma once

// Control channel messages (docs/PROTOCOL.md, "Control channel"). One struct per message type;
// field names and ranges follow the document. Coordinates are in 1/16 point.

#include "protocol/constants.hpp"
#include "protocol/error.hpp"
#include "protocol/wire.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace osdrop::protocol {

enum class ControlType : std::uint8_t {
    Hello = 0x01,
    HandshakeMessage = 0x02,
    PairingConfirm = 0x03,
    Capabilities = 0x04,
    SessionSetup = 0x05,
    Ping = 0x06,
    Pong = 0x07,
    Heartbeat = 0x08,
    Error = 0x09,
    Bye = 0x0A,
    RosterState = 0x10,
    ScreenReport = 0x11,
    LayoutState = 0x12,
    Enter = 0x20,
    Leave = 0x21,
    Refuse = 0x22,
    KeyEvent = 0x23,
    ButtonEvent = 0x24,
    ScrollEvent = 0x25,
    ReleaseAll = 0x26,
    CursorReport = 0x27,
    LocalTakeover = 0x28,
    DragProbe = 0x30,
    DragOffer = 0x31,
    DragResult = 0x32,
    ClipboardAnnounce = 0x40,
};

enum class Platform : std::uint8_t { MacOS = 1, Windows = 2 };
enum class PairingDecision : std::uint8_t { Accepted = 1, Rejected = 2 };
enum class ByeReason : std::uint8_t {
    UserDisconnected = 1,
    ApplicationQuit = 2,
    Sleeping = 3,
    Error = 4,
    LeavingGroup = 5,
    Revoked = 6,
    Duplicate = 7,
};
enum class LeaveReason : std::uint8_t {
    CrossedOn = 1,
    Returned = 2,
    ReleasedByHotkey = 3,
    ControllerLost = 4,
};
enum class RefuseReason : std::uint8_t { Locked = 1, Asleep = 2, NotPermitted = 3, Busy = 4 };
enum class KeyAction : std::uint8_t { Down = 1, Up = 2, Repeat = 3 };
enum class ButtonAction : std::uint8_t { Down = 1, Up = 2 };
enum class ScrollUnit : std::uint8_t { Lines = 1, Points = 2 };
enum class ScrollPhase : std::uint8_t {
    None = 0,
    MayBegin = 1,
    Began = 2,
    Changed = 3,
    Ended = 4,
    Cancelled = 5,
};
enum class MomentumPhase : std::uint8_t { None = 0, Began = 1, Changed = 2, Ended = 3 };
enum class ItemKind : std::uint8_t { File = 1, Folder = 2 };
enum class DragOutcome : std::uint8_t { Dropped = 1, Cancelled = 2, Failed = 3 };
enum class ClipboardFormat : std::uint8_t {
    PlainText = 1,
    RichText = 2,
    Html = 3,
    Image = 4,
    Files = 5,
};

inline constexpr std::uint16_t kKeyboardPage = 7;
inline constexpr std::uint16_t kConsumerPage = 12;

inline constexpr std::uint32_t kFeatureTakesControl = 1U << 0U;
inline constexpr std::uint32_t kFeatureDragSource = 1U << 1U;
inline constexpr std::uint32_t kFeatureDragTarget = 1U << 2U;
inline constexpr std::uint32_t kFeatureClipboard = 1U << 3U;
inline constexpr std::uint32_t kFeatureFiles = 1U << 4U;

struct Hello {
    static constexpr ControlType kType = ControlType::Hello;
    Platform platform = Platform::MacOS;
    std::uint8_t protocol_minor = kMinorVersion;
    Id group_id{};
    std::string device_name;
    bool operator==(const Hello&) const = default;
};

struct HandshakeMessage {
    static constexpr ControlType kType = ControlType::HandshakeMessage;
    std::uint8_t step = 1;
    std::vector<std::uint8_t> payload;
    bool operator==(const HandshakeMessage&) const = default;
};

struct PairingConfirm {
    static constexpr ControlType kType = ControlType::PairingConfirm;
    PairingDecision decision = PairingDecision::Accepted;
    bool operator==(const PairingConfirm&) const = default;
};

struct Capabilities {
    static constexpr ControlType kType = ControlType::Capabilities;
    std::uint32_t features = 0;
    std::uint8_t max_transfer_streams = 1;
    bool operator==(const Capabilities&) const = default;
};

struct SessionSetup {
    static constexpr ControlType kType = ControlType::SessionSetup;
    std::uint16_t udp_port = 0;
    std::uint16_t listen_port = 0;
    Id input_token{};
    Id transfer_token{};
    bool operator==(const SessionSetup&) const = default;
};

struct Ping {
    static constexpr ControlType kType = ControlType::Ping;
    std::uint32_t ping_id = 0;
    std::uint64_t send_time_us = 0;
    bool operator==(const Ping&) const = default;
};

struct Pong {
    static constexpr ControlType kType = ControlType::Pong;
    std::uint32_t ping_id = 0;
    std::uint64_t ping_send_time_us = 0;
    std::uint64_t receive_time_us = 0;
    std::uint64_t reply_time_us = 0;
    bool operator==(const Pong&) const = default;
};

struct Heartbeat {
    static constexpr ControlType kType = ControlType::Heartbeat;
    bool operator==(const Heartbeat&) const = default;
};

/// The Error message (type 0x09); named PeerError to keep it apart from protocol::Error.
struct PeerError {
    static constexpr ControlType kType = ControlType::Error;
    std::uint16_t code = 0;
    bool operator==(const PeerError&) const = default;
};

struct Bye {
    static constexpr ControlType kType = ControlType::Bye;
    ByeReason reason = ByeReason::UserDisconnected;
    bool operator==(const Bye&) const = default;
};

struct RosterMember {
    Id device_id{};
    PublicKey public_key{};
    std::uint32_t epoch = 1;
    Platform platform = Platform::MacOS;
    std::string name;
    bool operator==(const RosterMember&) const = default;
};

struct Revocation {
    Id device_id{};
    std::uint32_t epoch = 1;
    bool operator==(const Revocation&) const = default;
};

struct RosterState {
    static constexpr ControlType kType = ControlType::RosterState;
    Id group_id{};
    std::vector<RosterMember> members;
    std::vector<Revocation> revocations;
    bool operator==(const RosterState&) const = default;
};

struct Monitor {
    Id key{};
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::uint16_t pixel_width = 0;
    std::uint16_t pixel_height = 0;
    bool primary = false;
    bool operator==(const Monitor&) const = default;
};

struct ScreenReport {
    static constexpr ControlType kType = ControlType::ScreenReport;
    std::vector<Monitor> monitors;
    bool operator==(const ScreenReport&) const = default;
};

struct Placement {
    Id device_id{};
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool detached = false;
    bool operator==(const Placement&) const = default;
};

struct LayoutState {
    static constexpr ControlType kType = ControlType::LayoutState;
    std::uint64_t revision = 0;
    Id author{};
    std::vector<Placement> placements;
    bool operator==(const LayoutState&) const = default;
};

struct Enter {
    static constexpr ControlType kType = ControlType::Enter;
    std::uint32_t control_id = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint8_t modifiers = 0;
    std::uint32_t drag_id = 0;
    bool operator==(const Enter&) const = default;
};

struct Leave {
    static constexpr ControlType kType = ControlType::Leave;
    std::uint32_t control_id = 0;
    LeaveReason reason = LeaveReason::CrossedOn;
    bool operator==(const Leave&) const = default;
};

struct Refuse {
    static constexpr ControlType kType = ControlType::Refuse;
    std::uint32_t control_id = 0;
    RefuseReason reason = RefuseReason::Locked;
    bool operator==(const Refuse&) const = default;
};

struct KeyEvent {
    static constexpr ControlType kType = ControlType::KeyEvent;
    std::uint32_t control_id = 0;
    std::uint16_t page = kKeyboardPage;
    std::uint16_t usage = 0;
    KeyAction action = KeyAction::Down;
    bool operator==(const KeyEvent&) const = default;
};

struct ButtonEvent {
    static constexpr ControlType kType = ControlType::ButtonEvent;
    std::uint32_t control_id = 0;
    std::uint8_t button = 0;
    ButtonAction action = ButtonAction::Down;
    std::uint8_t click_count = 1;
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool operator==(const ButtonEvent&) const = default;
};

struct ScrollEvent {
    static constexpr ControlType kType = ControlType::ScrollEvent;
    std::uint32_t control_id = 0;
    ScrollUnit unit = ScrollUnit::Lines;
    ScrollPhase phase = ScrollPhase::None;
    MomentumPhase momentum = MomentumPhase::None;
    std::int32_t dx = 0; ///< 1/256 of a unit
    std::int32_t dy = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool operator==(const ScrollEvent&) const = default;
};

struct ReleaseAll {
    static constexpr ControlType kType = ControlType::ReleaseAll;
    std::uint32_t control_id = 0;
    bool operator==(const ReleaseAll&) const = default;
};

struct CursorReport {
    static constexpr ControlType kType = ControlType::CursorReport;
    std::uint32_t control_id = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool operator==(const CursorReport&) const = default;
};

struct LocalTakeover {
    static constexpr ControlType kType = ControlType::LocalTakeover;
    std::uint32_t control_id = 0;
    bool operator==(const LocalTakeover&) const = default;
};

struct DragProbe {
    static constexpr ControlType kType = ControlType::DragProbe;
    std::uint32_t probe_id = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool operator==(const DragProbe&) const = default;
};

struct OfferedItem {
    ItemKind kind = ItemKind::File;
    std::uint64_t byte_count = 0;
    std::string content_type;
    std::string name;
    bool operator==(const OfferedItem&) const = default;
};

struct DragOffer {
    static constexpr ControlType kType = ControlType::DragOffer;
    std::uint32_t probe_id = 0;
    std::uint32_t drag_id = 0; ///< 0: no drag in progress, and no items
    std::vector<OfferedItem> items;
    bool operator==(const DragOffer&) const = default;
};

struct DragResult {
    static constexpr ControlType kType = ControlType::DragResult;
    std::uint32_t drag_id = 0;
    DragOutcome outcome = DragOutcome::Dropped;
    bool operator==(const DragResult&) const = default;
};

struct AnnouncedFormat {
    ClipboardFormat format = ClipboardFormat::PlainText;
    std::uint64_t byte_count = 0;
    bool operator==(const AnnouncedFormat&) const = default;
};

struct ClipboardAnnounce {
    static constexpr ControlType kType = ControlType::ClipboardAnnounce;
    std::uint64_t clip_id = 0;
    std::vector<AnnouncedFormat> formats;
    bool operator==(const ClipboardAnnounce&) const = default;
};

using ControlMessage =
    std::variant<Hello, HandshakeMessage, PairingConfirm, Capabilities, SessionSetup, Ping, Pong,
                 Heartbeat, PeerError, Bye, RosterState, ScreenReport, LayoutState, Enter, Leave,
                 Refuse, KeyEvent, ButtonEvent, ScrollEvent, ReleaseAll, CursorReport,
                 LocalTakeover, DragProbe, DragOffer, DragResult, ClipboardAnnounce>;

[[nodiscard]] ControlType type_of(const ControlMessage& message);

/// Decodes one control message (the frame payload, without the length prefix).
[[nodiscard]] Expected<ControlMessage, Error> decode_control(std::span<const std::uint8_t> bytes);

/// Encodes one control message; fails with the error a decoder would report for invalid values,
/// or TooLarge when it does not fit in kMaxControlMessageSize.
[[nodiscard]] Expected<std::vector<std::uint8_t>, Error>
encode_control(const ControlMessage& message);

} // namespace osdrop::protocol
