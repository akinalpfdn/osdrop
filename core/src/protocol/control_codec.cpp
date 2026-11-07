#include "protocol/byte_io.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/wire.hpp"

#include <cstddef>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace osdrop::protocol {
namespace {

using Decoded = Expected<ControlMessage, Error>;

// The standard visitor idiom: one call operator per lambda.
template <typename... Visitors>
struct Overloaded : Visitors... { // NOLINT(misc-multiple-inheritance)
    using Visitors::operator()...;
};

Decoded decode_failure(Error error) {
    return Decoded::failure(error);
}

// --- Validation, shared by encoder and decoder ---------------------------------------------------

Check validate(const Hello& m) {
    if (!enum_in(m.platform, 1, 2)) {
        return fail(Error::ValueOutOfRange);
    }
    return check_string(m.device_name);
}

Check validate(const HandshakeMessage& m) {
    return range(in_range(m.step, 1, 3) && in_range(m.payload.size(), 1, kMaxHandshakePayload));
}

Check validate(const PairingConfirm& m) {
    return range(enum_in(m.decision, 1, 2));
}

Check validate(const Capabilities& m) {
    return range((m.features & ~kKnownFeatures) == 0 &&
                 in_range(m.max_transfer_streams, 1, kMaxTransferStreams));
}

Check validate(const SessionSetup& m) {
    return range(m.udp_port != 0 && m.listen_port != 0 && !is_zero(m.input_token) &&
                 !is_zero(m.transfer_token));
}

Check validate(const Ping& /*m*/) {
    return ok();
}
Check validate(const Pong& /*m*/) {
    return ok();
}
Check validate(const Heartbeat& /*m*/) {
    return ok();
}
Check validate(const PeerError& m) {
    return range(m.code != 0); // codes from newer versions are accepted and shown generically
}

Check validate(const Bye& m) {
    return range(enum_in(m.reason, 1, 7));
}

Check validate(const RosterState& m) {
    if (is_zero(m.group_id) || !in_range(m.members.size(), 1, kMaxMembers) ||
        m.revocations.size() > kMaxRevocations) {
        return fail(Error::ValueOutOfRange);
    }
    std::set<std::pair<Id, std::uint32_t>> members;
    for (const RosterMember& member : m.members) {
        if (member.epoch == 0 || !enum_in(member.platform, 1, 2) ||
            !members.emplace(member.device_id, member.epoch).second) {
            return fail(Error::ValueOutOfRange);
        }
        if (const Check name = check_string(member.name); !name) {
            return name;
        }
    }
    std::set<std::pair<Id, std::uint32_t>> revocations;
    for (const Revocation& revocation : m.revocations) {
        if (revocation.epoch == 0 ||
            !revocations.emplace(revocation.device_id, revocation.epoch).second) {
            return fail(Error::ValueOutOfRange);
        }
    }
    return ok();
}

bool is_monitor_size(std::int32_t value) {
    return value >= kMinMonitorSize && value <= kMaxMonitorSize;
}

Check validate(const ScreenReport& m) {
    if (!in_range(m.monitors.size(), 1, kMaxMonitors)) {
        return fail(Error::ValueOutOfRange);
    }
    std::set<Id> keys;
    std::size_t primaries = 0;
    for (const Monitor& monitor : m.monitors) {
        const bool valid = keys.insert(monitor.key).second && is_point(monitor.x, monitor.y) &&
                           is_monitor_size(monitor.width) && is_monitor_size(monitor.height) &&
                           is_point(monitor.native_x, monitor.native_y) &&
                           is_monitor_size(monitor.native_width) &&
                           is_monitor_size(monitor.native_height) &&
                           in_range(monitor.pixel_width, 1, kMaxPixelSize) &&
                           in_range(monitor.pixel_height, 1, kMaxPixelSize);
        if (!valid) {
            return fail(Error::ValueOutOfRange);
        }
        if (monitor.primary) {
            ++primaries;
        }
    }
    return range(primaries == 1);
}

Check validate(const LayoutState& m) {
    if (!in_range(m.placements.size(), 1, kMaxPlacements)) {
        return fail(Error::ValueOutOfRange);
    }
    std::set<Id> devices;
    for (const Placement& placement : m.placements) {
        if (!devices.insert(placement.device_id).second || !is_point(placement.x, placement.y)) {
            return fail(Error::ValueOutOfRange);
        }
    }
    return ok();
}

Check validate(const Enter& m) {
    return range(m.control_id != 0 && is_point(m.x, m.y));
}

Check validate(const Leave& m) {
    return range(m.control_id != 0 && enum_in(m.reason, 1, 4));
}

Check validate(const Refuse& m) {
    return range(m.control_id != 0 && enum_in(m.reason, 1, 4));
}

Check validate(const KeyEvent& m) {
    const bool page_ok = m.page == kKeyboardPage || m.page == kConsumerPage;
    return range(m.control_id != 0 && page_ok && m.usage != 0 && enum_in(m.action, 1, 3));
}

Check validate(const ButtonEvent& m) {
    return range(m.control_id != 0 && m.button <= kMaxButton && enum_in(m.action, 1, 2) &&
                 m.click_count != 0 && is_point(m.x, m.y));
}

Check validate(const ScrollEvent& m) {
    return range(m.control_id != 0 && enum_in(m.unit, 1, 2) && enum_in(m.phase, 0, 5) &&
                 enum_in(m.momentum, 0, 3) && is_point(m.x, m.y));
}

Check validate(const ReleaseAll& m) {
    return range(m.control_id != 0);
}

Check validate(const CursorReport& m) {
    return range(m.control_id != 0 && is_point(m.x, m.y));
}

Check validate(const LocalTakeover& m) {
    return range(m.control_id != 0);
}

Check validate(const DragProbe& m) {
    return range(m.probe_id != 0 && is_point(m.x, m.y));
}

Check validate(const DragOffer& m) {
    if (m.drag_id == 0) {
        // "No drag here" answers a probe and carries nothing.
        return range(m.probe_id != 0 && m.items.empty() && is_zero(m.source));
    }
    if (is_zero(m.source) || !in_range(m.items.size(), 1, kMaxDragItems)) {
        return fail(Error::ValueOutOfRange);
    }
    for (const OfferedItem& item : m.items) {
        if (!enum_in(item.kind, 1, 2)) {
            return fail(Error::ValueOutOfRange);
        }
        if (const Check type = check_type_id(item.content_type); !type) {
            return type;
        }
        if (const Check name = check_name(item.name); !name) {
            return name;
        }
    }
    return ok();
}

Check validate(const DragResult& m) {
    return range(m.drag_id != 0 && enum_in(m.outcome, 1, 3));
}

Check validate(const ClipboardAnnounce& m) {
    if (m.clip_id == 0 || !in_range(m.formats.size(), 1, kMaxClipboardFormats)) {
        return fail(Error::ValueOutOfRange);
    }
    std::set<ClipboardFormat> seen;
    for (const AnnouncedFormat& format : m.formats) {
        if (!enum_in(format.format, 1, 5) || !seen.insert(format.format).second) {
            return fail(Error::ValueOutOfRange);
        }
    }
    return ok();
}

// --- Decoding ------------------------------------------------------------------------------------
// Readers fail with Truncated when the input ends, check counts before allocating, and report a
// reserved field or flag bit that is not 0 as ValueOutOfRange once the whole body is read.

using BodyReader = FieldReader;

Decoded finish(const BodyReader& b, ControlMessage message) {
    if (!b.reserved_zero()) {
        return decode_failure(Error::ValueOutOfRange);
    }
    return message;
}

Decoded read_hello(BodyReader& b) {
    Hello m;
    Reader& r = b.raw();
    if (!b.enumeration(m.platform) || !r.u8(m.protocol_minor) || !b.reserved(2) ||
        !read_array(r, m.group_id) || !read_string(r, m.device_name)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, std::move(m));
}

Decoded read_handshake(BodyReader& b) {
    HandshakeMessage m;
    Reader& r = b.raw();
    std::uint16_t length = 0;
    std::span<const std::uint8_t> payload;
    if (!r.u8(m.step) || !b.reserved(1) || !r.u16(length) || !r.bytes(length, payload)) {
        return decode_failure(Error::Truncated);
    }
    m.payload.assign(payload.begin(), payload.end());
    return finish(b, std::move(m));
}

Decoded read_pairing_confirm(BodyReader& b) {
    PairingConfirm m;
    if (!b.enumeration(m.decision)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

Decoded read_capabilities(BodyReader& b) {
    Capabilities m;
    if (!b.raw().u32(m.features) || !b.raw().u8(m.max_transfer_streams) || !b.reserved(3)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

Decoded read_session_setup(BodyReader& b) {
    SessionSetup m;
    Reader& r = b.raw();
    if (!r.u16(m.udp_port) || !r.u16(m.listen_port) || !read_array(r, m.input_token) ||
        !read_array(r, m.transfer_token)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

Decoded read_ping(BodyReader& b) {
    Ping m;
    if (!b.raw().u32(m.ping_id) || !b.raw().u64(m.send_time_us)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

Decoded read_pong(BodyReader& b) {
    Pong m;
    Reader& r = b.raw();
    if (!r.u32(m.ping_id) || !r.u64(m.ping_send_time_us) || !r.u64(m.receive_time_us) ||
        !r.u64(m.reply_time_us)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

Decoded read_peer_error(BodyReader& b) {
    PeerError m;
    if (!b.raw().u16(m.code)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

Decoded read_bye(BodyReader& b) {
    Bye m;
    if (!b.enumeration(m.reason)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

Decoded read_roster_state(BodyReader& b) {
    RosterState m;
    Reader& r = b.raw();
    std::uint8_t member_count = 0;
    std::uint8_t revocation_count = 0;
    if (!read_array(r, m.group_id) || !r.u8(member_count) || !r.u8(revocation_count)) {
        return decode_failure(Error::Truncated);
    }
    if (!in_range(member_count, 1, kMaxMembers)) {
        return decode_failure(Error::ValueOutOfRange);
    }
    m.members.resize(member_count);
    for (RosterMember& member : m.members) {
        if (!read_array(r, member.device_id) || !read_array(r, member.public_key) ||
            !r.u32(member.epoch) || !b.enumeration(member.platform) || !b.reserved(1) ||
            !read_string(r, member.name)) {
            return decode_failure(Error::Truncated);
        }
    }
    m.revocations.resize(revocation_count);
    for (Revocation& revocation : m.revocations) {
        if (!read_array(r, revocation.device_id) || !r.u32(revocation.epoch)) {
            return decode_failure(Error::Truncated);
        }
    }
    return finish(b, std::move(m));
}

Decoded read_screen_report(BodyReader& b) {
    ScreenReport m;
    Reader& r = b.raw();
    std::uint8_t count = 0;
    if (!r.u8(count)) {
        return decode_failure(Error::Truncated);
    }
    if (!in_range(count, 1, kMaxMonitors)) {
        return decode_failure(Error::ValueOutOfRange);
    }
    m.monitors.resize(count);
    for (Monitor& monitor : m.monitors) {
        std::uint8_t flags = 0;
        if (!read_array(r, monitor.key) || !r.i32(monitor.x) || !r.i32(monitor.y) ||
            !r.i32(monitor.width) || !r.i32(monitor.height) || !r.i32(monitor.native_x) ||
            !r.i32(monitor.native_y) || !r.i32(monitor.native_width) ||
            !r.i32(monitor.native_height) || !r.u16(monitor.pixel_width) ||
            !r.u16(monitor.pixel_height) || !b.flags(1, flags) || !b.reserved(1)) {
            return decode_failure(Error::Truncated);
        }
        monitor.primary = (flags & 1U) != 0;
    }
    return finish(b, std::move(m));
}

Decoded read_layout_state(BodyReader& b) {
    LayoutState m;
    Reader& r = b.raw();
    std::uint8_t count = 0;
    if (!r.u64(m.revision) || !read_array(r, m.author) || !r.u8(count)) {
        return decode_failure(Error::Truncated);
    }
    if (!in_range(count, 1, kMaxPlacements)) {
        return decode_failure(Error::ValueOutOfRange);
    }
    m.placements.resize(count);
    for (Placement& placement : m.placements) {
        std::uint8_t flags = 0;
        if (!read_array(r, placement.device_id) || !r.i32(placement.x) || !r.i32(placement.y) ||
            !b.flags(1, flags) || !b.reserved(3)) {
            return decode_failure(Error::Truncated);
        }
        placement.detached = (flags & 1U) != 0;
    }
    return finish(b, std::move(m));
}

Decoded read_enter(BodyReader& b) {
    Enter m;
    Reader& r = b.raw();
    if (!r.u32(m.control_id) || !r.i32(m.x) || !r.i32(m.y) || !r.u8(m.modifiers) ||
        !b.flags(kKnownLocks, m.locks) || !b.reserved(2) || !r.u32(m.drag_id)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

template <typename ReasonMessage>
Decoded read_with_reason(BodyReader& b) {
    ReasonMessage m;
    if (!b.raw().u32(m.control_id) || !b.enumeration(m.reason)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

Decoded read_key_event(BodyReader& b) {
    KeyEvent m;
    Reader& r = b.raw();
    if (!r.u32(m.control_id) || !r.u16(m.page) || !r.u16(m.usage) || !b.enumeration(m.action) ||
        !b.reserved(1)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

Decoded read_button_event(BodyReader& b) {
    ButtonEvent m;
    Reader& r = b.raw();
    if (!r.u32(m.control_id) || !r.u8(m.button) || !b.enumeration(m.action) ||
        !r.u8(m.click_count) || !b.reserved(1) || !r.i32(m.x) || !r.i32(m.y)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

Decoded read_scroll_event(BodyReader& b) {
    ScrollEvent m;
    Reader& r = b.raw();
    if (!r.u32(m.control_id) || !b.enumeration(m.unit) || !b.enumeration(m.phase) ||
        !b.enumeration(m.momentum) || !b.reserved(1) || !r.i32(m.dx) || !r.i32(m.dy) ||
        !r.i32(m.x) || !r.i32(m.y)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

template <typename ControlOnly>
Decoded read_control_only(BodyReader& b) {
    ControlOnly m;
    if (!b.raw().u32(m.control_id)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

Decoded read_cursor_report(BodyReader& b) {
    CursorReport m;
    if (!b.raw().u32(m.control_id) || !b.raw().i32(m.x) || !b.raw().i32(m.y)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

Decoded read_drag_probe(BodyReader& b) {
    DragProbe m;
    if (!b.raw().u32(m.probe_id) || !b.raw().i32(m.x) || !b.raw().i32(m.y)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

Decoded read_drag_offer(BodyReader& b) {
    DragOffer m;
    Reader& r = b.raw();
    std::uint16_t count = 0;
    if (!r.u32(m.probe_id) || !r.u32(m.drag_id) || !read_array(r, m.source) || !r.u16(count)) {
        return decode_failure(Error::Truncated);
    }
    if (count > kMaxDragItems) {
        return decode_failure(Error::ValueOutOfRange);
    }
    m.items.resize(count);
    for (OfferedItem& item : m.items) {
        if (!b.enumeration(item.kind) || !b.reserved(1) || !r.u64(item.byte_count) ||
            !read_string(r, item.content_type) || !read_long_string(r, item.name)) {
            return decode_failure(Error::Truncated);
        }
    }
    return finish(b, std::move(m));
}

Decoded read_drag_result(BodyReader& b) {
    DragResult m;
    if (!b.raw().u32(m.drag_id) || !b.enumeration(m.outcome)) {
        return decode_failure(Error::Truncated);
    }
    return finish(b, m);
}

Decoded read_clipboard_announce(BodyReader& b) {
    ClipboardAnnounce m;
    Reader& r = b.raw();
    std::uint8_t count = 0;
    if (!r.u64(m.clip_id) || !r.u8(count)) {
        return decode_failure(Error::Truncated);
    }
    if (!in_range(count, 1, kMaxClipboardFormats)) {
        return decode_failure(Error::ValueOutOfRange);
    }
    m.formats.resize(count);
    for (AnnouncedFormat& format : m.formats) {
        if (!b.enumeration(format.format) || !b.reserved(3) || !r.u64(format.byte_count)) {
            return decode_failure(Error::Truncated);
        }
    }
    return finish(b, std::move(m));
}

Decoded read_body(std::uint8_t type, BodyReader& b) {
    switch (static_cast<ControlType>(type)) {
    case ControlType::Hello:
        return read_hello(b);
    case ControlType::HandshakeMessage:
        return read_handshake(b);
    case ControlType::PairingConfirm:
        return read_pairing_confirm(b);
    case ControlType::Capabilities:
        return read_capabilities(b);
    case ControlType::SessionSetup:
        return read_session_setup(b);
    case ControlType::Ping:
        return read_ping(b);
    case ControlType::Pong:
        return read_pong(b);
    case ControlType::Heartbeat:
        return finish(b, Heartbeat{});
    case ControlType::Error:
        return read_peer_error(b);
    case ControlType::Bye:
        return read_bye(b);
    case ControlType::RosterState:
        return read_roster_state(b);
    case ControlType::ScreenReport:
        return read_screen_report(b);
    case ControlType::LayoutState:
        return read_layout_state(b);
    case ControlType::Enter:
        return read_enter(b);
    case ControlType::Leave:
        return read_with_reason<Leave>(b);
    case ControlType::Refuse:
        return read_with_reason<Refuse>(b);
    case ControlType::KeyEvent:
        return read_key_event(b);
    case ControlType::ButtonEvent:
        return read_button_event(b);
    case ControlType::ScrollEvent:
        return read_scroll_event(b);
    case ControlType::ReleaseAll:
        return read_control_only<ReleaseAll>(b);
    case ControlType::CursorReport:
        return read_cursor_report(b);
    case ControlType::LocalTakeover:
        return read_control_only<LocalTakeover>(b);
    case ControlType::DragProbe:
        return read_drag_probe(b);
    case ControlType::DragOffer:
        return read_drag_offer(b);
    case ControlType::DragResult:
        return read_drag_result(b);
    case ControlType::ClipboardAnnounce:
        return read_clipboard_announce(b);
    }
    return decode_failure(Error::UnknownType);
}

// --- Encoding ------------------------------------------------------------------------------------

void write_point(Writer& w, std::int32_t x, std::int32_t y) {
    w.i32(x);
    w.i32(y);
}

void write_body(Writer& w, const ControlMessage& message) {
    std::visit(Overloaded{
                   [&](const Hello& m) {
                       write_enum(w, m.platform);
                       w.u8(m.protocol_minor);
                       write_reserved(w, 2);
                       w.bytes(m.group_id);
                       write_string(w, m.device_name);
                   },
                   [&](const HandshakeMessage& m) {
                       w.u8(m.step);
                       write_reserved(w, 1);
                       w.u16(static_cast<std::uint16_t>(m.payload.size()));
                       w.bytes(m.payload);
                   },
                   [&](const PairingConfirm& m) { write_enum(w, m.decision); },
                   [&](const Capabilities& m) {
                       w.u32(m.features);
                       w.u8(m.max_transfer_streams);
                       write_reserved(w, 3);
                   },
                   [&](const SessionSetup& m) {
                       w.u16(m.udp_port);
                       w.u16(m.listen_port);
                       w.bytes(m.input_token);
                       w.bytes(m.transfer_token);
                   },
                   [&](const Ping& m) {
                       w.u32(m.ping_id);
                       w.u64(m.send_time_us);
                   },
                   [&](const Pong& m) {
                       w.u32(m.ping_id);
                       w.u64(m.ping_send_time_us);
                       w.u64(m.receive_time_us);
                       w.u64(m.reply_time_us);
                   },
                   [&](const Heartbeat& /*m*/) {},
                   [&](const PeerError& m) { w.u16(m.code); },
                   [&](const Bye& m) { write_enum(w, m.reason); },
                   [&](const RosterState& m) {
                       w.bytes(m.group_id);
                       w.u8(static_cast<std::uint8_t>(m.members.size()));
                       w.u8(static_cast<std::uint8_t>(m.revocations.size()));
                       for (const RosterMember& member : m.members) {
                           w.bytes(member.device_id);
                           w.bytes(member.public_key);
                           w.u32(member.epoch);
                           write_enum(w, member.platform);
                           write_reserved(w, 1);
                           write_string(w, member.name);
                       }
                       for (const Revocation& revocation : m.revocations) {
                           w.bytes(revocation.device_id);
                           w.u32(revocation.epoch);
                       }
                   },
                   [&](const ScreenReport& m) {
                       w.u8(static_cast<std::uint8_t>(m.monitors.size()));
                       for (const Monitor& monitor : m.monitors) {
                           w.bytes(monitor.key);
                           write_point(w, monitor.x, monitor.y);
                           w.i32(monitor.width);
                           w.i32(monitor.height);
                           write_point(w, monitor.native_x, monitor.native_y);
                           w.i32(monitor.native_width);
                           w.i32(monitor.native_height);
                           w.u16(monitor.pixel_width);
                           w.u16(monitor.pixel_height);
                           w.u8(static_cast<std::uint8_t>(monitor.primary));
                           write_reserved(w, 1);
                       }
                   },
                   [&](const LayoutState& m) {
                       w.u64(m.revision);
                       w.bytes(m.author);
                       w.u8(static_cast<std::uint8_t>(m.placements.size()));
                       for (const Placement& placement : m.placements) {
                           w.bytes(placement.device_id);
                           write_point(w, placement.x, placement.y);
                           w.u8(static_cast<std::uint8_t>(placement.detached));
                           write_reserved(w, 3);
                       }
                   },
                   [&](const Enter& m) {
                       w.u32(m.control_id);
                       write_point(w, m.x, m.y);
                       w.u8(m.modifiers);
                       w.u8(m.locks);
                       write_reserved(w, 2);
                       w.u32(m.drag_id);
                   },
                   [&](const Leave& m) {
                       w.u32(m.control_id);
                       write_enum(w, m.reason);
                   },
                   [&](const Refuse& m) {
                       w.u32(m.control_id);
                       write_enum(w, m.reason);
                   },
                   [&](const KeyEvent& m) {
                       w.u32(m.control_id);
                       w.u16(m.page);
                       w.u16(m.usage);
                       write_enum(w, m.action);
                       write_reserved(w, 1);
                   },
                   [&](const ButtonEvent& m) {
                       w.u32(m.control_id);
                       w.u8(m.button);
                       write_enum(w, m.action);
                       w.u8(m.click_count);
                       write_reserved(w, 1);
                       write_point(w, m.x, m.y);
                   },
                   [&](const ScrollEvent& m) {
                       w.u32(m.control_id);
                       write_enum(w, m.unit);
                       write_enum(w, m.phase);
                       write_enum(w, m.momentum);
                       write_reserved(w, 1);
                       w.i32(m.dx);
                       w.i32(m.dy);
                       write_point(w, m.x, m.y);
                   },
                   [&](const ReleaseAll& m) { w.u32(m.control_id); },
                   [&](const CursorReport& m) {
                       w.u32(m.control_id);
                       write_point(w, m.x, m.y);
                   },
                   [&](const LocalTakeover& m) { w.u32(m.control_id); },
                   [&](const DragProbe& m) {
                       w.u32(m.probe_id);
                       write_point(w, m.x, m.y);
                   },
                   [&](const DragOffer& m) {
                       w.u32(m.probe_id);
                       w.u32(m.drag_id);
                       w.bytes(m.source);
                       w.u16(static_cast<std::uint16_t>(m.items.size()));
                       for (const OfferedItem& item : m.items) {
                           write_enum(w, item.kind);
                           write_reserved(w, 1);
                           w.u64(item.byte_count);
                           write_string(w, item.content_type);
                           write_long_string(w, item.name);
                       }
                   },
                   [&](const DragResult& m) {
                       w.u32(m.drag_id);
                       write_enum(w, m.outcome);
                   },
                   [&](const ClipboardAnnounce& m) {
                       w.u64(m.clip_id);
                       w.u8(static_cast<std::uint8_t>(m.formats.size()));
                       for (const AnnouncedFormat& format : m.formats) {
                           write_enum(w, format.format);
                           write_reserved(w, 3);
                           w.u64(format.byte_count);
                       }
                   },
               },
               message);
}

} // namespace

ControlType type_of(const ControlMessage& message) {
    return std::visit([](const auto& m) { return std::remove_cvref_t<decltype(m)>::kType; },
                      message);
}

Expected<ControlMessage, Error> decode_control(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > kMaxControlMessageSize) {
        return decode_failure(Error::TooLarge);
    }
    Reader reader(bytes);
    std::uint8_t type = 0;
    if (const Check prefix = read_prefix(reader, type); !prefix) {
        return decode_failure(prefix.error());
    }
    BodyReader body(reader);
    auto decoded = read_body(type, body);
    if (!decoded) {
        return decoded;
    }
    if (reader.remaining() != 0) {
        return decode_failure(Error::TrailingBytes);
    }
    const Check valid = std::visit([](const auto& m) { return validate(m); }, decoded.value());
    if (!valid) {
        return decode_failure(valid.error());
    }
    return decoded;
}

Expected<std::vector<std::uint8_t>, Error> encode_control(const ControlMessage& message) {
    using Result = Expected<std::vector<std::uint8_t>, Error>;
    const Check valid = std::visit([](const auto& m) { return validate(m); }, message);
    if (!valid) {
        return Result::failure(valid.error());
    }
    std::vector<std::uint8_t> buffer(kMaxControlMessageSize);
    Writer writer(buffer);
    write_prefix(writer, static_cast<std::uint8_t>(type_of(message)));
    write_body(writer, message);
    if (!writer.ok()) {
        return Result::failure(Error::TooLarge);
    }
    buffer.resize(writer.size());
    return buffer;
}

} // namespace osdrop::protocol
