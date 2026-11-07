#!/usr/bin/env python3
"""Writes protocol/fixtures from docs/PROTOCOL.md, independently of the C++ implementation.

Usage:
  generate_fixtures.py          write every fixture and the manifest
  generate_fixtures.py --check  exit 1 if the files on disk differ from what would be written
"""

from __future__ import annotations

import struct
import sys
import unicodedata
from pathlib import Path
from typing import NamedTuple

ROOT = Path(__file__).resolve().parent.parent
FIXTURES = ROOT / "fixtures"

MAGIC = b"OD"
VERSION = 1

# osdrop_result codes (docs/PROTOCOL.md, "Error codes")
TRUNCATED = 3
BAD_MAGIC = 4
UNSUPPORTED_VERSION = 5
UNKNOWN_TYPE = 6
TRAILING_BYTES = 7
VALUE_OUT_OF_RANGE = 8
INVALID_STRING = 9
TOO_LARGE = 10

MAX_CONTROL = 65536
MAX_TRANSFER = 65536
MAX_INPUT_PLAINTEXT = 1200 - 16
COORD_LIMIT = 1 << 28
MAX_CHUNK = 65504


class Fixture(NamedTuple):
    name: str
    kind: str  # control, input or transfer
    outcome: str  # valid or error
    detail: int  # message type when valid, error code otherwise
    description: str
    data: bytes


# --- Field encoders ----------------------------------------------------------------------------

def u8(v: int) -> bytes:
    return struct.pack("<B", v)


def u16(v: int) -> bytes:
    return struct.pack("<H", v)


def u32(v: int) -> bytes:
    return struct.pack("<I", v)


def u64(v: int) -> bytes:
    return struct.pack("<Q", v)


def i32(v: int) -> bytes:
    return struct.pack("<i", v)


def i64(v: int) -> bytes:
    return struct.pack("<q", v)


def raw(text: str | bytes) -> bytes:
    return text.encode("utf-8") if isinstance(text, str) else text


def string(text: str | bytes) -> bytes:
    """`string` and `type_id`: u8 length."""
    data = raw(text)
    return u8(len(data)) + data


def long_string(text: str | bytes) -> bytes:
    """`name` and `path`: u16 length."""
    data = raw(text)
    return u16(len(data)) + data


def pattern(seed: int, length: int = 16) -> bytes:
    return bytes((seed + i * 7) % 256 for i in range(length))


GROUP = pattern(0x40)
MAC_ID = pattern(0x10)
PC_ID = pattern(0x20)
LAPTOP_ID = pattern(0x30)
NO_GROUP = bytes(16)
ZERO_ID = bytes(16)
INPUT_TOKEN = bytes(range(0xA0, 0xB0))
TRANSFER_TOKEN = bytes(range(0xC0, 0xD0))
DIGEST = pattern(0x77, 32)


def coords(x: int, y: int) -> bytes:
    return i32(x) + i32(y)


# --- Control messages --------------------------------------------------------------------------

def control(message_type: int, body: bytes = b"", magic: bytes = MAGIC, version: int = VERSION) -> bytes:
    return magic + u8(version) + u8(message_type) + body


def hello(platform: int = 1, name: str | bytes = "MacBook Air", group: bytes = NO_GROUP, minor: int = 0,
          reserved: int = 0) -> bytes:
    return control(0x01, u8(platform) + u8(minor) + u16(reserved) + group + string(name))


def handshake(step: int, payload: bytes, declared_length: int | None = None, reserved: int = 0) -> bytes:
    length = len(payload) if declared_length is None else declared_length
    return control(0x02, u8(step) + u8(reserved) + u16(length) + payload)


def capabilities(features: int, streams: int, reserved: bytes = bytes(3)) -> bytes:
    return control(0x04, u32(features) + u8(streams) + reserved)


def session_setup(port: int = 50_123, listen_port: int = 49_152, input_token: bytes = INPUT_TOKEN,
                  transfer_token: bytes = TRANSFER_TOKEN) -> bytes:
    return control(0x05, u16(port) + u16(listen_port) + input_token + transfer_token)


def member(device_id: bytes, epoch: int, platform: int, name: str | bytes, reserved: int = 0) -> bytes:
    return device_id + pattern(device_id[0] + 1, 32) + u32(epoch) + u8(platform) + u8(reserved) + string(name)


def revocation(device_id: bytes, epoch: int) -> bytes:
    return device_id + u32(epoch)


def roster(members: list[bytes], revocations: list[bytes] | None = None, group: bytes = GROUP,
           member_count: int | None = None) -> bytes:
    revoked = revocations or []
    count = len(members) if member_count is None else member_count
    return control(0x10, group + u8(count) + u8(len(revoked)) + b"".join(members) + b"".join(revoked))


def monitor(key: bytes, x: int, y: int, width: int, height: int, pixel_width: int, pixel_height: int,
            flags: int = 0, reserved: int = 0, native: tuple[int, int, int, int] | None = None) -> bytes:
    """`native` is the monitor in native cursor units; macOS monitors use their frame."""
    nx, ny, nw, nh = native if native is not None else (x, y, width, height)
    return (key + coords(x, y) + i32(width) + i32(height) + coords(nx, ny) + i32(nw) + i32(nh)
            + u16(pixel_width) + u16(pixel_height) + u8(flags) + u8(reserved))


def built_in(flags: int = 1) -> bytes:
    """A 1470 x 956 point Retina display at the origin."""
    return monitor(pattern(1), 0, 0, 1470 * 16, 956 * 16, 2940, 1912, flags)


def screen_report(monitors: list[bytes], count: int | None = None) -> bytes:
    return control(0x11, u8(len(monitors) if count is None else count) + b"".join(monitors))


def placement(device_id: bytes, x: int, y: int, flags: int = 0, reserved: bytes = bytes(3)) -> bytes:
    return device_id + coords(x, y) + u8(flags) + reserved


def layout(placements: list[bytes], count: int | None = None, revision: int = 12,
           author: bytes = MAC_ID) -> bytes:
    return control(0x12, u64(revision) + author + u8(len(placements) if count is None else count)
                   + b"".join(placements))


def enter(control_id: int = 7, x: int = 0, y: int = 4_800, modifiers: int = 0, locks: int = 0, drag_id: int = 0,
          reserved: bytes = bytes(2)) -> bytes:
    return control(0x20, u32(control_id) + coords(x, y) + u8(modifiers) + u8(locks) + reserved + u32(drag_id))


def key_event(page: int = 7, usage: int = 0x04, action: int = 1, control_id: int = 7, reserved: int = 0) -> bytes:
    return control(0x23, u32(control_id) + u16(page) + u16(usage) + u8(action) + u8(reserved))


def button_event(button: int = 0, action: int = 1, click_count: int = 1, control_id: int = 7, reserved: int = 0,
                 x: int = 1_600, y: int = 3_200) -> bytes:
    return control(0x24, u32(control_id) + u8(button) + u8(action) + u8(click_count) + u8(reserved) + coords(x, y))


def scroll_event(unit: int = 2, phase: int = 3, momentum: int = 0, reserved: int = 0, dx: int = 0,
                 dy: int = -640, control_id: int = 7) -> bytes:
    return control(0x25, u32(control_id) + u8(unit) + u8(phase) + u8(momentum) + u8(reserved) + i32(dx)
                   + i32(dy) + coords(8_000, 6_400))


def offered_item(kind: int, byte_count: int, content_type: str | bytes, name: str | bytes,
                 reserved: int = 0) -> bytes:
    return u8(kind) + u8(reserved) + u64(byte_count) + string(content_type) + long_string(name)


PDF = offered_item(1, 482_113, "com.adobe.pdf", "Report.pdf")
FOLDER = offered_item(2, 3_221_225_472, "public.folder", "Photos 2026")


def drag_offer(probe_id: int, drag_id: int, items: list[bytes], count: int | None = None,
               source: bytes | None = None) -> bytes:
    if source is None:
        source = ZERO_ID if drag_id == 0 else MAC_ID
    return control(0x31, u32(probe_id) + u32(drag_id) + source + u16(len(items) if count is None else count)
                   + b"".join(items))


def max_size_drag_offer() -> bytes:
    """A DragOffer of exactly 65536 bytes: 234 items with 255-byte names and one with a 196-byte name."""
    def item(index: int, name_length: int) -> bytes:
        name = f"{index:04d}" + "n" * (name_length - 4)
        return offered_item(1, index, "public.data", name)
    items = [item(i, 255) for i in range(234)] + [item(234, 196)]
    return drag_offer(1, 2, items)


def clipboard(formats: list[bytes], clip_id: int = 0x0102_0304_0506, count: int | None = None) -> bytes:
    return control(0x40, u64(clip_id) + u8(len(formats) if count is None else count) + b"".join(formats))


def clip_format(code: int, byte_count: int, reserved: bytes = bytes(3)) -> bytes:
    return u8(code) + reserved + u64(byte_count)


def control_fixtures() -> list[Fixture]:
    v, e = "valid", "error"
    edge = COORD_LIMIT
    nfd = unicodedata.normalize("NFD", "Çalışma Özeti.txt")
    roster_two = [member(MAC_ID, 1, 1, "MacBook Air"), member(PC_ID, 3, 2, "Akınalp'ın PC")]
    many_members = [member(pattern(i), 1, 1 + i % 2, f"Computer {i:02d} " + "x" * 50) for i in range(64)]
    many_revocations = [revocation(pattern(i + 100), 1 + i) for i in range(255)]
    return [
        # Valid: every message type, with boundary values.
        Fixture("control/hello_macos.bin", "control", v, 0x01, "Hello from a Mac in no group", hello()),
        Fixture("control/hello_windows_group.bin", "control", v, 0x01,
                "Hello from a PC in a group, UTF-8 name", hello(2, "Akınalp'ın PC — Çalışma", GROUP)),
        Fixture("control/hello_name_64_bytes.bin", "control", v, 0x01, "64-byte device name",
                hello(name="a" * 64)),
        Fixture("control/handshake_step1.bin", "control", v, 0x02, "Noise XX message 1",
                handshake(1, bytes(range(32)))),
        Fixture("control/handshake_max_payload.bin", "control", v, 0x02, "message 2 with a 1024-byte payload",
                handshake(2, pattern(3, 1024))),
        Fixture("control/pairing_accept.bin", "control", v, 0x03, "PairingConfirm accepted", control(0x03, u8(1))),
        Fixture("control/pairing_reject.bin", "control", v, 0x03, "PairingConfirm rejected", control(0x03, u8(2))),
        Fixture("control/capabilities_all.bin", "control", v, 0x04, "every feature, eight transfer streams",
                capabilities(0x1F, 8)),
        Fixture("control/capabilities_none.bin", "control", v, 0x04, "no feature allowed, one stream",
                capabilities(0, 1)),
        Fixture("control/session_setup.bin", "control", v, 0x05, "SessionSetup", session_setup()),
        Fixture("control/ping.bin", "control", v, 0x06, "Ping", control(0x06, u32(77) + u64(1_000_000))),
        Fixture("control/pong.bin", "control", v, 0x07, "Pong",
                control(0x07, u32(77) + u64(1_000_000) + u64(9_000_500) + u64(9_000_620))),
        Fixture("control/heartbeat.bin", "control", v, 0x08, "Heartbeat", control(0x08)),
        Fixture("control/error_not_permitted.bin", "control", v, 0x09, "Error NOT_PERMITTED",
                control(0x09, u16(28))),
        Fixture("control/error_unknown_code.bin", "control", v, 0x09, "Error with a code from a newer version",
                control(0x09, u16(999))),
        Fixture("control/bye_revoked.bin", "control", v, 0x0A, "Bye: revoked", control(0x0A, u8(6))),
        Fixture("control/bye_duplicate.bin", "control", v, 0x0A, "Bye: duplicate connection",
                control(0x0A, u8(7))),
        Fixture("control/roster_two_members.bin", "control", v, 0x10, "two members and a revocation",
                roster(roster_two, [revocation(LAPTOP_ID, 1)])),
        Fixture("control/roster_repaired_member.bin", "control", v, 0x10,
                "the same device at epochs 1 and 2, epoch 1 revoked",
                roster([member(PC_ID, 1, 2, "PC"), member(PC_ID, 2, 2, "PC")], [revocation(PC_ID, 1)])),
        Fixture("control/roster_largest.bin", "control", v, 0x10, "64 members and 255 revocations",
                roster(many_members, many_revocations)),
        Fixture("control/screen_report_two.bin", "control", v, 0x11,
                "built-in display and an external one on its left",
                screen_report([built_in(), monitor(pattern(2), -2560 * 16, -240 * 16, 2560 * 16, 1440 * 16,
                                                   5120, 2880)])),
        Fixture("control/screen_report_mixed_scale.bin", "control", v, 0x11,
                "a PC: a 150% laptop panel and a 100% monitor on its right, in physical pixels",
                screen_report([monitor(pattern(5), 0, 0, 1280 * 16, 800 * 16, 1920, 1200, 1,
                                       native=(0, 0, 1920 * 16, 1200 * 16)),
                               monitor(pattern(6), 1280 * 16, 0, 2560 * 16, 1440 * 16, 2560, 1440,
                                       native=(1920 * 16, 0, 2560 * 16, 1440 * 16))])),
        Fixture("control/screen_report_bounds.bin", "control", v, 0x11,
                "smallest and largest monitor sizes at the coordinate limits",
                screen_report([monitor(pattern(3), -edge, -edge, 16, 16, 1, 1, 1),
                               monitor(pattern(4), edge, edge, 16 * 32768, 16 * 32768, 32768, 32768)])),
        Fixture("control/layout_three.bin", "control", v, 0x12, "three computers, one detached",
                layout([placement(MAC_ID, 0, 0), placement(PC_ID, 1470 * 16, -100 * 16),
                        placement(LAPTOP_ID, -edge, edge, flags=1)])),
        Fixture("control/enter.bin", "control", v, 0x20, "Enter with Shift held", enter(modifiers=0b10)),
        Fixture("control/enter_caps_lock.bin", "control", v, 0x20, "Enter with Caps Lock on",
                enter(locks=1)),
        Fixture("control/enter_with_drag.bin", "control", v, 0x20, "Enter carrying a drag",
                enter(control_id=0xFFFF_FFFF, x=-edge, y=edge, drag_id=9)),
        Fixture("control/leave_crossed_on.bin", "control", v, 0x21, "Leave: crossed on",
                control(0x21, u32(7) + u8(1))),
        Fixture("control/refuse_locked.bin", "control", v, 0x22, "Refuse: locked", control(0x22, u32(7) + u8(1))),
        Fixture("control/key_down_a.bin", "control", v, 0x23, "key A down", key_event()),
        Fixture("control/key_repeat_consumer.bin", "control", v, 0x23, "volume up repeat",
                key_event(page=12, usage=0xE9, action=3)),
        Fixture("control/button_right_down.bin", "control", v, 0x24, "right button down", button_event(1)),
        Fixture("control/button_31_up.bin", "control", v, 0x24, "button 31 up", button_event(31, 2)),
        Fixture("control/button_double_click.bin", "control", v, 0x24, "second click of a double-click, down",
                button_event(click_count=2)),
        Fixture("control/button_click_255_up.bin", "control", v, 0x24, "up of the 255th click",
                button_event(action=2, click_count=255)),
        Fixture("control/scroll_trackpad.bin", "control", v, 0x25, "trackpad scroll, changed phase",
                scroll_event()),
        Fixture("control/scroll_wheel_momentum.bin", "control", v, 0x25, "line scroll with momentum ending",
                scroll_event(unit=1, phase=0, momentum=3, dx=256, dy=-(2 ** 31))),
        Fixture("control/release_all.bin", "control", v, 0x26, "ReleaseAll", control(0x26, u32(7))),
        Fixture("control/cursor_report.bin", "control", v, 0x27, "CursorReport",
                control(0x27, u32(7) + coords(-16, 33))),
        Fixture("control/local_takeover.bin", "control", v, 0x28, "LocalTakeover", control(0x28, u32(7))),
        Fixture("control/drag_probe.bin", "control", v, 0x30, "DragProbe", control(0x30, u32(5) + coords(0, 960))),
        Fixture("control/drag_offer_none.bin", "control", v, 0x31, "no drag in progress",
                drag_offer(5, 0, [])),
        Fixture("control/drag_offer_two.bin", "control", v, 0x31, "a file and a folder",
                drag_offer(5, 9, [PDF, FOLDER])),
        Fixture("control/drag_offer_forwarded.bin", "control", v, 0x31,
                "forwarded to the target, emoji name in NFC, started on another computer", drag_offer(0, 9, [
                    offered_item(1, 0, "public.png", "Ekran Görüntüsü 🎉.png")], source=PC_ID)),
        Fixture("control/drag_offer_nfd_name.bin", "control", v, 0x31,
                "decomposed (NFD) name: decoders do not check the form",
                drag_offer(5, 9, [offered_item(1, 12, "public.plain-text", nfd)])),
        Fixture("control/drag_offer_max_size.bin", "control", v, 0x31, "DragOffer of exactly 65536 bytes",
                max_size_drag_offer()),
        Fixture("control/drag_result_dropped.bin", "control", v, 0x32, "DragResult dropped",
                control(0x32, u32(9) + u8(1))),
        Fixture("control/clipboard_text_image.bin", "control", v, 0x40, "text, HTML and an image",
                clipboard([clip_format(1, 1_204), clip_format(3, 5_310), clip_format(4, 2_400_000)])),
        Fixture("control/clipboard_all_formats.bin", "control", v, 0x40, "every format",
                clipboard([clip_format(code, code * 1000) for code in (5, 4, 3, 2, 1)], clip_id=2 ** 64 - 1)),

        # Common prefix and size.
        Fixture("malformed/control_empty.bin", "control", e, TRUNCATED, "empty input", b""),
        Fixture("malformed/control_prefix_only.bin", "control", e, TRUNCATED, "prefix without type",
                MAGIC + u8(VERSION)),
        Fixture("malformed/control_bad_magic.bin", "control", e, BAD_MAGIC, "magic is OP (Osplay)",
                control(0x08, magic=b"OP")),
        Fixture("malformed/control_version_2.bin", "control", e, UNSUPPORTED_VERSION, "version 2",
                control(0x08, version=2)),
        Fixture("malformed/control_type_0.bin", "control", e, UNKNOWN_TYPE, "type 0x00", control(0x00)),
        Fixture("malformed/control_type_7f.bin", "control", e, UNKNOWN_TYPE, "type 0x7F", control(0x7F)),
        Fixture("malformed/control_transfer_type.bin", "control", e, UNKNOWN_TYPE,
                "type 0x88, a transfer type", control(0x88, u32(4))),
        Fixture("malformed/control_too_large.bin", "control", e, TOO_LARGE, "65537-byte message",
                max_size_drag_offer() + b"n"),

        # Hello.
        Fixture("malformed/hello_trailing_byte.bin", "control", e, TRAILING_BYTES, "one extra byte",
                hello() + b"\x00"),
        Fixture("malformed/hello_platform_3.bin", "control", e, VALUE_OUT_OF_RANGE, "platform 3",
                hello(platform=3)),
        Fixture("malformed/hello_reserved_set.bin", "control", e, VALUE_OUT_OF_RANGE, "reserved is 0x0100",
                hello(reserved=0x0100)),
        Fixture("malformed/hello_empty_name.bin", "control", e, INVALID_STRING, "empty device name",
                hello(name="")),
        Fixture("malformed/hello_name_65_bytes.bin", "control", e, INVALID_STRING, "65-byte name",
                hello(name="a" * 65)),
        Fixture("malformed/hello_invalid_utf8.bin", "control", e, INVALID_STRING, "name is not UTF-8",
                hello(name=b"Mac\xff")),
        Fixture("malformed/hello_overlong_utf8.bin", "control", e, INVALID_STRING, "overlong encoding of '/'",
                hello(name=b"\xc0\xaf")),
        Fixture("malformed/hello_surrogate.bin", "control", e, INVALID_STRING, "encoded surrogate U+D800",
                hello(name=b"a\xed\xa0\x80")),
        Fixture("malformed/hello_newline.bin", "control", e, INVALID_STRING, "name contains a newline",
                hello(name="Mac\nBook")),
        Fixture("malformed/hello_c1_control.bin", "control", e, INVALID_STRING, "name contains U+0085",
                hello(name="Mac\u0085")),
        Fixture("malformed/hello_cut_utf8.bin", "control", e, INVALID_STRING, "name ends inside a character",
                hello(name=b"Mac\xc3")),
        Fixture("malformed/hello_name_past_end.bin", "control", e, TRUNCATED, "name length points past the end",
                hello()[:-1]),

        # HandshakeMessage, PairingConfirm, Capabilities, SessionSetup, Error, Bye, Heartbeat, Ping.
        Fixture("malformed/handshake_step_0.bin", "control", e, VALUE_OUT_OF_RANGE, "step 0",
                handshake(0, b"\x01")),
        Fixture("malformed/handshake_step_4.bin", "control", e, VALUE_OUT_OF_RANGE, "step 4",
                handshake(4, b"\x01")),
        Fixture("malformed/handshake_empty_payload.bin", "control", e, VALUE_OUT_OF_RANGE, "payload length 0",
                handshake(1, b"")),
        Fixture("malformed/handshake_payload_1025.bin", "control", e, VALUE_OUT_OF_RANGE, "1025-byte payload",
                handshake(2, bytes(1025))),
        Fixture("malformed/handshake_reserved_set.bin", "control", e, VALUE_OUT_OF_RANGE, "reserved is 1",
                handshake(1, b"\x01", reserved=1)),
        Fixture("malformed/handshake_short_payload.bin", "control", e, TRUNCATED, "payload shorter than declared",
                handshake(1, bytes(10), declared_length=32)),
        Fixture("malformed/pairing_decision_3.bin", "control", e, VALUE_OUT_OF_RANGE, "decision 3",
                control(0x03, u8(3))),
        Fixture("malformed/capabilities_reserved_feature.bin", "control", e, VALUE_OUT_OF_RANGE, "feature bit 5",
                capabilities(0x20, 1)),
        Fixture("malformed/capabilities_streams_0.bin", "control", e, VALUE_OUT_OF_RANGE, "zero transfer streams",
                capabilities(0x1F, 0)),
        Fixture("malformed/capabilities_streams_9.bin", "control", e, VALUE_OUT_OF_RANGE, "nine transfer streams",
                capabilities(0x1F, 9)),
        Fixture("malformed/capabilities_reserved_set.bin", "control", e, VALUE_OUT_OF_RANGE,
                "last reserved byte is 1", capabilities(0x1F, 4, b"\x00\x00\x01")),
        Fixture("malformed/session_setup_port_0.bin", "control", e, VALUE_OUT_OF_RANGE, "UDP port 0",
                session_setup(port=0)),
        Fixture("malformed/session_setup_listen_port_0.bin", "control", e, VALUE_OUT_OF_RANGE,
                "listen port 0", session_setup(listen_port=0)),
        Fixture("malformed/session_setup_zero_input_token.bin", "control", e, VALUE_OUT_OF_RANGE,
                "input token all zeros", session_setup(input_token=ZERO_ID)),
        Fixture("malformed/session_setup_zero_transfer_token.bin", "control", e, VALUE_OUT_OF_RANGE,
                "transfer token all zeros", session_setup(transfer_token=ZERO_ID)),
        Fixture("malformed/session_setup_short_token.bin", "control", e, TRUNCATED, "transfer token cut short",
                session_setup()[:-1]),
        Fixture("malformed/error_code_0.bin", "control", e, VALUE_OUT_OF_RANGE, "code 0", control(0x09, u16(0))),
        Fixture("malformed/bye_reason_0.bin", "control", e, VALUE_OUT_OF_RANGE, "reason 0", control(0x0A, u8(0))),
        Fixture("malformed/bye_reason_8.bin", "control", e, VALUE_OUT_OF_RANGE, "reason 8", control(0x0A, u8(8))),
        Fixture("malformed/heartbeat_with_body.bin", "control", e, TRAILING_BYTES, "Heartbeat with a body",
                control(0x08, b"\x00")),
        Fixture("malformed/ping_truncated.bin", "control", e, TRUNCATED, "send time cut short",
                control(0x06, u32(1) + bytes(7))),

        # RosterState.
        Fixture("malformed/roster_zero_group.bin", "control", e, VALUE_OUT_OF_RANGE, "group ID all zeros",
                roster(roster_two, group=ZERO_ID)),
        Fixture("malformed/roster_no_members.bin", "control", e, VALUE_OUT_OF_RANGE, "member_count 0",
                roster([])),
        Fixture("malformed/roster_65_members.bin", "control", e, VALUE_OUT_OF_RANGE, "member_count 65",
                roster(many_members, member_count=65)),
        Fixture("malformed/roster_member_count_mismatch.bin", "control", e, TRUNCATED,
                "member_count 3, two members", roster(roster_two, member_count=3)),
        Fixture("malformed/roster_epoch_0.bin", "control", e, VALUE_OUT_OF_RANGE, "member epoch 0",
                roster([member(MAC_ID, 0, 1, "Mac")])),
        Fixture("malformed/roster_platform_0.bin", "control", e, VALUE_OUT_OF_RANGE, "member platform 0",
                roster([member(MAC_ID, 1, 0, "Mac")])),
        Fixture("malformed/roster_member_reserved.bin", "control", e, VALUE_OUT_OF_RANGE,
                "member reserved byte is 1", roster([member(MAC_ID, 1, 1, "Mac", reserved=1)])),
        Fixture("malformed/roster_member_empty_name.bin", "control", e, INVALID_STRING, "member name empty",
                roster([member(MAC_ID, 1, 1, "")])),
        Fixture("malformed/roster_duplicate_member.bin", "control", e, VALUE_OUT_OF_RANGE,
                "same device and epoch twice", roster([member(PC_ID, 2, 2, "PC"), member(PC_ID, 2, 2, "PC 2")])),
        Fixture("malformed/roster_revocation_epoch_0.bin", "control", e, VALUE_OUT_OF_RANGE,
                "revocation epoch 0", roster(roster_two, [revocation(LAPTOP_ID, 0)])),
        Fixture("malformed/roster_duplicate_revocation.bin", "control", e, VALUE_OUT_OF_RANGE,
                "same revocation twice", roster(roster_two, [revocation(LAPTOP_ID, 1), revocation(LAPTOP_ID, 1)])),
        Fixture("malformed/roster_revocation_cut.bin", "control", e, TRUNCATED, "revocation epoch cut short",
                roster(roster_two, [revocation(LAPTOP_ID, 1)])[:-2]),

        # ScreenReport.
        Fixture("malformed/screen_none.bin", "control", e, VALUE_OUT_OF_RANGE, "monitor_count 0",
                screen_report([])),
        Fixture("malformed/screen_17_monitors.bin", "control", e, VALUE_OUT_OF_RANGE, "monitor_count 17",
                screen_report([built_in()], count=17)),
        Fixture("malformed/screen_duplicate_key.bin", "control", e, VALUE_OUT_OF_RANGE, "same key twice",
                screen_report([built_in(), built_in(0)])),
        Fixture("malformed/screen_no_primary.bin", "control", e, VALUE_OUT_OF_RANGE, "no primary monitor",
                screen_report([built_in(0)])),
        Fixture("malformed/screen_two_primaries.bin", "control", e, VALUE_OUT_OF_RANGE, "two primary monitors",
                screen_report([built_in(), monitor(pattern(2), 23_520, 0, 16_000, 9_000, 1000, 562, 1)])),
        Fixture("malformed/screen_width_15.bin", "control", e, VALUE_OUT_OF_RANGE, "width below one point",
                screen_report([monitor(pattern(1), 0, 0, 15, 16, 1, 1, 1)])),
        Fixture("malformed/screen_width_too_large.bin", "control", e, VALUE_OUT_OF_RANGE,
                "width above 32768 points", screen_report([monitor(pattern(1), 0, 0, 16 * 32768 + 1, 16, 1, 1, 1)])),
        Fixture("malformed/screen_negative_height.bin", "control", e, VALUE_OUT_OF_RANGE, "height -16",
                screen_report([monitor(pattern(1), 0, 0, 16, -16, 1, 1, 1)])),
        Fixture("malformed/screen_pixel_width_0.bin", "control", e, VALUE_OUT_OF_RANGE, "pixel width 0",
                screen_report([monitor(pattern(1), 0, 0, 16, 16, 0, 1, 1)])),
        Fixture("malformed/screen_pixel_height_32769.bin", "control", e, VALUE_OUT_OF_RANGE,
                "pixel height 32769", screen_report([monitor(pattern(1), 0, 0, 16, 16, 1, 32769, 1)])),
        Fixture("malformed/screen_x_outside.bin", "control", e, VALUE_OUT_OF_RANGE, "x one past 2^28",
                screen_report([monitor(pattern(1), edge + 1, 0, 16, 16, 1, 1, 1)])),
        Fixture("malformed/screen_native_width_15.bin", "control", e, VALUE_OUT_OF_RANGE,
                "native width below one unit",
                screen_report([monitor(pattern(1), 0, 0, 16, 16, 1, 1, 1, native=(0, 0, 15, 16))])),
        Fixture("malformed/screen_native_y_outside.bin", "control", e, VALUE_OUT_OF_RANGE,
                "native y one below -2^28",
                screen_report([monitor(pattern(1), 0, 0, 16, 16, 1, 1, 1, native=(0, -edge - 1, 16, 16))])),
        Fixture("malformed/screen_reserved_flag.bin", "control", e, VALUE_OUT_OF_RANGE, "flag bit 1",
                screen_report([built_in(0b11)])),
        Fixture("malformed/screen_reserved_set.bin", "control", e, VALUE_OUT_OF_RANGE, "reserved byte is 1",
                screen_report([monitor(pattern(1), 0, 0, 16, 16, 1, 1, 1, reserved=1)])),

        # LayoutState.
        Fixture("malformed/layout_none.bin", "control", e, VALUE_OUT_OF_RANGE, "placement_count 0", layout([])),
        Fixture("malformed/layout_65_placements.bin", "control", e, VALUE_OUT_OF_RANGE, "placement_count 65",
                layout([placement(MAC_ID, 0, 0)], count=65)),
        Fixture("malformed/layout_duplicate_device.bin", "control", e, VALUE_OUT_OF_RANGE, "same device twice",
                layout([placement(MAC_ID, 0, 0), placement(MAC_ID, 16, 0)])),
        Fixture("malformed/layout_y_outside.bin", "control", e, VALUE_OUT_OF_RANGE, "y one below -2^28",
                layout([placement(MAC_ID, 0, -edge - 1)])),
        Fixture("malformed/layout_reserved_flag.bin", "control", e, VALUE_OUT_OF_RANGE, "flag bit 7",
                layout([placement(MAC_ID, 0, 0, flags=0x80)])),
        Fixture("malformed/layout_reserved_set.bin", "control", e, VALUE_OUT_OF_RANGE, "reserved byte is 1",
                layout([placement(MAC_ID, 0, 0, reserved=b"\x00\x01\x00")])),
        Fixture("malformed/layout_placement_cut.bin", "control", e, TRUNCATED, "placement cut short",
                layout([placement(MAC_ID, 0, 0)])[:-1]),

        # Input control messages.
        Fixture("malformed/enter_control_0.bin", "control", e, VALUE_OUT_OF_RANGE, "control_id 0",
                enter(control_id=0)),
        Fixture("malformed/enter_x_outside.bin", "control", e, VALUE_OUT_OF_RANGE, "x is the largest i32",
                enter(x=2 ** 31 - 1)),
        Fixture("malformed/enter_reserved_set.bin", "control", e, VALUE_OUT_OF_RANGE, "reserved byte is 1",
                enter(reserved=b"\x01\x00")),
        Fixture("malformed/enter_unknown_lock.bin", "control", e, VALUE_OUT_OF_RANGE, "a lock bit beyond Caps Lock",
                enter(locks=0b10)),
        Fixture("malformed/leave_reason_0.bin", "control", e, VALUE_OUT_OF_RANGE, "reason 0",
                control(0x21, u32(7) + u8(0))),
        Fixture("malformed/leave_reason_5.bin", "control", e, VALUE_OUT_OF_RANGE, "reason 5",
                control(0x21, u32(7) + u8(5))),
        Fixture("malformed/refuse_control_0.bin", "control", e, VALUE_OUT_OF_RANGE, "control_id 0",
                control(0x22, u32(0) + u8(1))),
        Fixture("malformed/refuse_reason_5.bin", "control", e, VALUE_OUT_OF_RANGE, "reason 5",
                control(0x22, u32(7) + u8(5))),
        Fixture("malformed/key_page_8.bin", "control", e, VALUE_OUT_OF_RANGE, "usage page 8 (LEDs)",
                key_event(page=8)),
        Fixture("malformed/key_usage_0.bin", "control", e, VALUE_OUT_OF_RANGE, "usage 0", key_event(usage=0)),
        Fixture("malformed/key_action_4.bin", "control", e, VALUE_OUT_OF_RANGE, "action 4", key_event(action=4)),
        Fixture("malformed/key_control_0.bin", "control", e, VALUE_OUT_OF_RANGE, "control_id 0",
                key_event(control_id=0)),
        Fixture("malformed/key_reserved_set.bin", "control", e, VALUE_OUT_OF_RANGE, "reserved byte is 1",
                key_event(reserved=1)),
        Fixture("malformed/key_truncated.bin", "control", e, TRUNCATED, "reserved byte missing", key_event()[:-1]),
        Fixture("malformed/button_32.bin", "control", e, VALUE_OUT_OF_RANGE, "button 32", button_event(32)),
        Fixture("malformed/button_action_3.bin", "control", e, VALUE_OUT_OF_RANGE, "action 3",
                button_event(action=3)),
        Fixture("malformed/button_click_count_0.bin", "control", e, VALUE_OUT_OF_RANGE, "click_count 0",
                button_event(click_count=0)),
        Fixture("malformed/button_reserved_set.bin", "control", e, VALUE_OUT_OF_RANGE, "reserved byte is 1",
                button_event(reserved=1)),
        Fixture("malformed/button_y_outside.bin", "control", e, VALUE_OUT_OF_RANGE, "y is the smallest i32",
                button_event(y=-(2 ** 31))),
        Fixture("malformed/scroll_unit_0.bin", "control", e, VALUE_OUT_OF_RANGE, "unit 0", scroll_event(unit=0)),
        Fixture("malformed/scroll_unit_3.bin", "control", e, VALUE_OUT_OF_RANGE, "unit 3", scroll_event(unit=3)),
        Fixture("malformed/scroll_phase_6.bin", "control", e, VALUE_OUT_OF_RANGE, "phase 6", scroll_event(phase=6)),
        Fixture("malformed/scroll_momentum_4.bin", "control", e, VALUE_OUT_OF_RANGE, "momentum 4",
                scroll_event(momentum=4)),
        Fixture("malformed/scroll_reserved_set.bin", "control", e, VALUE_OUT_OF_RANGE, "reserved byte is 1",
                scroll_event(reserved=1)),
        Fixture("malformed/release_all_control_0.bin", "control", e, VALUE_OUT_OF_RANGE, "control_id 0",
                control(0x26, u32(0))),
        Fixture("malformed/cursor_report_x_outside.bin", "control", e, VALUE_OUT_OF_RANGE, "x one past 2^28",
                control(0x27, u32(7) + coords(edge + 1, 0))),
        Fixture("malformed/local_takeover_truncated.bin", "control", e, TRUNCATED, "control_id cut short",
                control(0x28, u16(7))),

        # Drag messages.
        Fixture("malformed/drag_probe_id_0.bin", "control", e, VALUE_OUT_OF_RANGE, "probe_id 0",
                control(0x30, u32(0) + coords(0, 0))),
        Fixture("malformed/drag_offer_none_with_items.bin", "control", e, VALUE_OUT_OF_RANGE,
                "no drag but one item", drag_offer(5, 0, [PDF])),
        Fixture("malformed/drag_offer_forwarded_none.bin", "control", e, VALUE_OUT_OF_RANGE,
                "forwarded offer without a drag", drag_offer(0, 0, [])),
        Fixture("malformed/drag_offer_no_items.bin", "control", e, VALUE_OUT_OF_RANGE, "a drag with no items",
                drag_offer(5, 9, [])),
        Fixture("malformed/drag_offer_no_source.bin", "control", e, VALUE_OUT_OF_RANGE,
                "a drag without its source", drag_offer(5, 9, [PDF], source=ZERO_ID)),
        Fixture("malformed/drag_offer_none_with_source.bin", "control", e, VALUE_OUT_OF_RANGE,
                "no drag but a source", drag_offer(5, 0, [], source=MAC_ID)),
        Fixture("malformed/drag_offer_1025_items.bin", "control", e, VALUE_OUT_OF_RANGE, "item_count 1025",
                drag_offer(5, 9, [PDF], count=1025)),
        Fixture("malformed/drag_offer_item_count_mismatch.bin", "control", e, TRUNCATED,
                "item_count 2, one item", drag_offer(5, 9, [PDF], count=2)),
        Fixture("malformed/drag_offer_kind_3.bin", "control", e, VALUE_OUT_OF_RANGE, "item kind 3",
                drag_offer(5, 9, [offered_item(3, 1, "public.data", "a")])),
        Fixture("malformed/drag_offer_item_reserved.bin", "control", e, VALUE_OUT_OF_RANGE,
                "item reserved byte is 1", drag_offer(5, 9, [offered_item(1, 1, "public.data", "a", reserved=1)])),
        Fixture("malformed/drag_offer_empty_type.bin", "control", e, INVALID_STRING, "empty content type",
                drag_offer(5, 9, [offered_item(1, 1, "", "a")])),
        Fixture("malformed/drag_offer_type_space.bin", "control", e, INVALID_STRING, "content type with a space",
                drag_offer(5, 9, [offered_item(1, 1, "public data", "a")])),
        Fixture("malformed/drag_offer_type_129.bin", "control", e, INVALID_STRING, "129-byte content type",
                drag_offer(5, 9, [offered_item(1, 1, "a" * 129, "a")])),
        Fixture("malformed/drag_offer_name_slash.bin", "control", e, INVALID_STRING, "name contains '/'",
                drag_offer(5, 9, [offered_item(1, 1, "public.data", "a/b")])),
        Fixture("malformed/drag_offer_name_dot.bin", "control", e, INVALID_STRING, "name is '.'",
                drag_offer(5, 9, [offered_item(2, 0, "public.folder", ".")])),
        Fixture("malformed/drag_offer_name_dot_dot.bin", "control", e, INVALID_STRING, "name is '..'",
                drag_offer(5, 9, [offered_item(2, 0, "public.folder", "..")])),
        Fixture("malformed/drag_offer_empty_name.bin", "control", e, INVALID_STRING, "empty name",
                drag_offer(5, 9, [offered_item(1, 1, "public.data", "")])),
        Fixture("malformed/drag_offer_name_256.bin", "control", e, INVALID_STRING, "256-byte name",
                drag_offer(5, 9, [offered_item(1, 1, "public.data", "a" * 256)])),
        Fixture("malformed/drag_offer_name_nul.bin", "control", e, INVALID_STRING, "name contains NUL",
                drag_offer(5, 9, [offered_item(1, 1, "public.data", "a\x00.txt")])),
        Fixture("malformed/drag_offer_name_cut.bin", "control", e, TRUNCATED, "name cut short",
                drag_offer(5, 9, [PDF])[:-3]),
        Fixture("malformed/drag_result_id_0.bin", "control", e, VALUE_OUT_OF_RANGE, "drag_id 0",
                control(0x32, u32(0) + u8(1))),
        Fixture("malformed/drag_result_outcome_4.bin", "control", e, VALUE_OUT_OF_RANGE, "outcome 4",
                control(0x32, u32(9) + u8(4))),

        # ClipboardAnnounce.
        Fixture("malformed/clipboard_id_0.bin", "control", e, VALUE_OUT_OF_RANGE, "clip_id 0",
                clipboard([clip_format(1, 5)], clip_id=0)),
        Fixture("malformed/clipboard_no_formats.bin", "control", e, VALUE_OUT_OF_RANGE, "format_count 0",
                clipboard([])),
        Fixture("malformed/clipboard_6_formats.bin", "control", e, VALUE_OUT_OF_RANGE, "format_count 6",
                clipboard([clip_format(1, 5)], count=6)),
        Fixture("malformed/clipboard_format_6.bin", "control", e, VALUE_OUT_OF_RANGE, "format 6",
                clipboard([clip_format(6, 5)])),
        Fixture("malformed/clipboard_duplicate_format.bin", "control", e, VALUE_OUT_OF_RANGE,
                "plain text twice", clipboard([clip_format(1, 5), clip_format(1, 6)])),
        Fixture("malformed/clipboard_reserved_set.bin", "control", e, VALUE_OUT_OF_RANGE, "reserved byte is 1",
                clipboard([clip_format(1, 5, reserved=b"\x00\x00\x01")])),
    ]


# --- Input datagrams ---------------------------------------------------------------------------

def datagram(datagram_type: int, sequence: int, body: bytes, magic: bytes = MAGIC, version: int = VERSION) -> bytes:
    return magic + u8(version) + u8(datagram_type) + u64(sequence) + body


def input_hello(token: bytes = INPUT_TOKEN, sequence: int = 0) -> bytes:
    return datagram(0x01, sequence, token)


def pointer_motion(control_id: int = 7, x: int = -1_600, y: int = 4_816, sequence: int = 1_234) -> bytes:
    return datagram(0x02, sequence, u32(control_id) + coords(x, y) + u64(5_000_123_456))


def input_fixtures() -> list[Fixture]:
    v, e = "valid", "error"
    return [
        Fixture("input/input_hello.bin", "input", v, 0x01, "InputHello", input_hello()),
        Fixture("input/pointer_motion.bin", "input", v, 0x02, "PointerMotion, negative x", pointer_motion()),
        Fixture("input/pointer_motion_last_sequence.bin", "input", v, 0x02,
                "largest sequence, coordinates at the limits",
                pointer_motion(x=COORD_LIMIT, y=-COORD_LIMIT, sequence=2 ** 64 - 1)),

        Fixture("malformed/input_empty.bin", "input", e, TRUNCATED, "empty input", b""),
        Fixture("malformed/input_header_cut.bin", "input", e, TRUNCATED, "header cut at 8 bytes",
                input_hello()[:8]),
        Fixture("malformed/input_bad_magic.bin", "input", e, BAD_MAGIC, "magic is OE",
                datagram(0x01, 0, INPUT_TOKEN, magic=b"OE")),
        Fixture("malformed/input_version_0.bin", "input", e, UNSUPPORTED_VERSION, "version 0",
                datagram(0x01, 0, INPUT_TOKEN, version=0)),
        Fixture("malformed/input_unknown_type.bin", "input", e, UNKNOWN_TYPE, "type 0x03",
                datagram(0x03, 0, INPUT_TOKEN)),
        Fixture("malformed/input_too_large.bin", "input", e, TOO_LARGE, "1185-byte plaintext datagram",
                input_hello() + bytes(MAX_INPUT_PLAINTEXT + 1 - len(input_hello()))),
        Fixture("malformed/input_hello_short_token.bin", "input", e, TRUNCATED, "15-byte token",
                input_hello(INPUT_TOKEN[:15])),
        Fixture("malformed/input_hello_trailing.bin", "input", e, TRAILING_BYTES, "17 bytes after the header",
                input_hello(INPUT_TOKEN + b"\x00")),
        Fixture("malformed/input_hello_zero_token.bin", "input", e, VALUE_OUT_OF_RANGE, "token all zeros",
                input_hello(ZERO_ID)),
        Fixture("malformed/pointer_motion_control_0.bin", "input", e, VALUE_OUT_OF_RANGE, "control_id 0",
                pointer_motion(control_id=0)),
        Fixture("malformed/pointer_motion_x_outside.bin", "input", e, VALUE_OUT_OF_RANGE, "x one past 2^28",
                pointer_motion(x=COORD_LIMIT + 1)),
        Fixture("malformed/pointer_motion_cut.bin", "input", e, TRUNCATED, "capture time cut short",
                pointer_motion()[:-1]),
    ]


# --- Transfer messages -------------------------------------------------------------------------

def transfer(message_type: int, body: bytes = b"", magic: bytes = MAGIC, version: int = VERSION) -> bytes:
    return magic + u8(version) + u8(message_type) + body


def transfer_hello(token: bytes = TRANSFER_TOKEN, stream_id: int = 1) -> bytes:
    return transfer(0x81, token + u32(stream_id))


def object_ref(kind: int = 1, object_id: int = 9, item_index: int = 0, reserved: bytes = bytes(3)) -> bytes:
    return u8(kind) + reserved + u64(object_id) + u32(item_index)


def manifest_request(request_id: int = 3, obj: bytes | None = None) -> bytes:
    return transfer(0x82, u32(request_id) + (object_ref() if obj is None else obj))


def entry(path: str | bytes, kind: int = 1, flags: int = 0, byte_count: int = 0,
          modified_us: int = 1_793_000_000_000_000, reserved: int = 0) -> bytes:
    return long_string(path) + u8(kind) + u8(flags) + u16(reserved) + u64(byte_count) + i64(modified_us)


def manifest(entries: list[bytes], request_id: int = 3, is_last: int = 1, count: int | None = None,
             reserved: int = 0) -> bytes:
    return transfer(0x83, u32(request_id) + u8(is_last) + u8(reserved)
                    + u16(len(entries) if count is None else count) + b"".join(entries))


def content_request(request_id: int = 4, obj: bytes | None = None, entry_index: int = 2, offset: int = 0,
                    length: int = 2 ** 64 - 1) -> bytes:
    return transfer(0x84, u32(request_id) + (object_ref() if obj is None else obj) + u32(entry_index)
                    + u64(offset) + u64(length))


def content_chunk(data: bytes, request_id: int = 4, offset: int = 65_504,
                  declared_length: int | None = None) -> bytes:
    length = len(data) if declared_length is None else declared_length
    return transfer(0x85, u32(request_id) + u64(offset) + u32(length) + data)


def transfer_fixtures() -> list[Fixture]:
    v, e = "valid", "error"
    clipboard_files = object_ref(2, 0x0102_0304_0506, 5)
    clipboard_text = object_ref(2, 0x0102_0304_0506, 1)
    tree = [
        entry("Photos 2026", kind=2),
        entry("Photos 2026/İstanbul.heic", byte_count=3_100_200),
        entry("Photos 2026/tools", kind=2, modified_us=-86_400_000_000),
        entry("Photos 2026/tools/resize.sh", flags=1, byte_count=812),
        entry("Photos 2026/latest", kind=3, byte_count=len("İstanbul.heic".encode())),
    ]
    return [
        Fixture("transfer/transfer_hello.bin", "transfer", v, 0x81, "TransferHello", transfer_hello()),
        Fixture("transfer/manifest_request_drag.bin", "transfer", v, 0x82, "manifest of dragged item 1",
                manifest_request(obj=object_ref(item_index=1))),
        Fixture("transfer/manifest_request_last_item.bin", "transfer", v, 0x82, "dragged item 1023",
                manifest_request(obj=object_ref(item_index=1023))),
        Fixture("transfer/manifest_request_clipboard_files.bin", "transfer", v, 0x82,
                "manifest of the clipboard's files", manifest_request(obj=clipboard_files)),
        Fixture("transfer/manifest_single_file.bin", "transfer", v, 0x83, "a file item's single entry",
                manifest([entry("Report.pdf", byte_count=482_113)])),
        Fixture("transfer/manifest_tree.bin", "transfer", v, 0x83,
                "a folder with a subfolder, an executable and a link, not the last part",
                manifest(tree, is_last=0)),
        Fixture("transfer/content_request_drag.bin", "transfer", v, 0x84, "entry 2 of dragged item 0, to the end",
                content_request()),
        Fixture("transfer/content_request_clipboard_text.bin", "transfer", v, 0x84, "the clipboard's plain text",
                content_request(obj=clipboard_text, entry_index=0, length=1)),
        Fixture("transfer/content_request_clipboard_file.bin", "transfer", v, 0x84,
                "entry 3 of the clipboard's files, resumed at 1 MiB",
                content_request(obj=clipboard_files, entry_index=3, offset=1 << 20, length=1 << 30)),
        Fixture("transfer/content_chunk.bin", "transfer", v, 0x85, "a short chunk", content_chunk(pattern(9, 100))),
        Fixture("transfer/content_chunk_max.bin", "transfer", v, 0x85, "a 65504-byte chunk",
                content_chunk(pattern(5, MAX_CHUNK))),
        Fixture("transfer/content_end.bin", "transfer", v, 0x86, "ContentEnd",
                transfer(0x86, u32(4) + u64(3_100_200) + DIGEST)),
        Fixture("transfer/transfer_error_stream.bin", "transfer", v, 0x87, "error for the whole stream",
                transfer(0x87, u32(0) + u16(28))),
        Fixture("transfer/transfer_cancel.bin", "transfer", v, 0x88, "TransferCancel", transfer(0x88, u32(4))),

        Fixture("malformed/transfer_empty.bin", "transfer", e, TRUNCATED, "empty input", b""),
        Fixture("malformed/transfer_bad_magic.bin", "transfer", e, BAD_MAGIC, "magic is od",
                transfer(0x88, u32(4), magic=b"od")),
        Fixture("malformed/transfer_version_2.bin", "transfer", e, UNSUPPORTED_VERSION, "version 2",
                transfer(0x88, u32(4), version=2)),
        Fixture("malformed/transfer_unknown_type.bin", "transfer", e, UNKNOWN_TYPE, "type 0x89",
                transfer(0x89, u32(4))),
        Fixture("malformed/transfer_control_type.bin", "transfer", e, UNKNOWN_TYPE,
                "type 0x01, a control type", transfer(0x01, TRANSFER_TOKEN + u32(1))),
        Fixture("malformed/transfer_too_large.bin", "transfer", e, TOO_LARGE, "65537-byte message",
                content_chunk(bytes(MAX_TRANSFER + 1 - 20))),
        Fixture("malformed/transfer_hello_zero_token.bin", "transfer", e, VALUE_OUT_OF_RANGE, "token all zeros",
                transfer_hello(ZERO_ID)),
        Fixture("malformed/transfer_hello_stream_0.bin", "transfer", e, VALUE_OUT_OF_RANGE, "stream_id 0",
                transfer_hello(stream_id=0)),
        Fixture("malformed/transfer_hello_cut.bin", "transfer", e, TRUNCATED, "stream_id cut short",
                transfer_hello()[:-2]),
        Fixture("malformed/manifest_request_id_0.bin", "transfer", e, VALUE_OUT_OF_RANGE, "request_id 0",
                manifest_request(request_id=0)),
        Fixture("malformed/manifest_request_kind_3.bin", "transfer", e, VALUE_OUT_OF_RANGE, "object_kind 3",
                manifest_request(obj=object_ref(kind=3))),
        Fixture("malformed/manifest_request_object_0.bin", "transfer", e, VALUE_OUT_OF_RANGE, "object_id 0",
                manifest_request(obj=object_ref(object_id=0))),
        Fixture("malformed/manifest_request_item_1024.bin", "transfer", e, VALUE_OUT_OF_RANGE,
                "dragged item 1024", manifest_request(obj=object_ref(item_index=1024))),
        Fixture("malformed/manifest_request_format_0.bin", "transfer", e, VALUE_OUT_OF_RANGE,
                "clipboard format 0", manifest_request(obj=object_ref(2, 1, 0))),
        Fixture("malformed/manifest_request_format_6.bin", "transfer", e, VALUE_OUT_OF_RANGE,
                "clipboard format 6", manifest_request(obj=object_ref(2, 1, 6))),
        Fixture("malformed/manifest_request_reserved.bin", "transfer", e, VALUE_OUT_OF_RANGE,
                "reserved byte is 1", manifest_request(obj=object_ref(reserved=b"\x00\x01\x00"))),
        Fixture("malformed/manifest_id_0.bin", "transfer", e, VALUE_OUT_OF_RANGE, "request_id 0",
                manifest([entry("a")], request_id=0)),
        Fixture("malformed/manifest_is_last_2.bin", "transfer", e, VALUE_OUT_OF_RANGE, "is_last 2",
                manifest([entry("a")], is_last=2)),
        Fixture("malformed/manifest_reserved_set.bin", "transfer", e, VALUE_OUT_OF_RANGE, "reserved byte is 1",
                manifest([entry("a")], reserved=1)),
        Fixture("malformed/manifest_no_entries.bin", "transfer", e, VALUE_OUT_OF_RANGE, "entry_count 0",
                manifest([])),
        Fixture("malformed/manifest_4097_entries.bin", "transfer", e, VALUE_OUT_OF_RANGE, "entry_count 4097",
                manifest([entry("a")], count=4097)),
        Fixture("malformed/manifest_count_mismatch.bin", "transfer", e, TRUNCATED, "entry_count 2, one entry",
                manifest([entry("a")], count=2)),
        Fixture("malformed/manifest_kind_0.bin", "transfer", e, VALUE_OUT_OF_RANGE, "entry kind 0",
                manifest([entry("a", kind=0)])),
        Fixture("malformed/manifest_kind_4.bin", "transfer", e, VALUE_OUT_OF_RANGE, "entry kind 4",
                manifest([entry("a", kind=4)])),
        Fixture("malformed/manifest_reserved_flag.bin", "transfer", e, VALUE_OUT_OF_RANGE, "flag bit 1",
                manifest([entry("a", flags=0b10)])),
        Fixture("malformed/manifest_entry_reserved.bin", "transfer", e, VALUE_OUT_OF_RANGE,
                "entry reserved is 1", manifest([entry("a", reserved=1)])),
        Fixture("malformed/manifest_folder_executable.bin", "transfer", e, VALUE_OUT_OF_RANGE,
                "executable folder", manifest([entry("a", kind=2, flags=1)])),
        Fixture("malformed/manifest_folder_size.bin", "transfer", e, VALUE_OUT_OF_RANGE, "folder with a size",
                manifest([entry("a", kind=2, byte_count=1)])),
        Fixture("malformed/manifest_link_empty.bin", "transfer", e, VALUE_OUT_OF_RANGE, "link without a target",
                manifest([entry("a", kind=3)])),
        Fixture("malformed/manifest_link_4097.bin", "transfer", e, VALUE_OUT_OF_RANGE, "4097-byte link target",
                manifest([entry("a", kind=3, byte_count=4097)])),
        Fixture("malformed/manifest_link_executable.bin", "transfer", e, VALUE_OUT_OF_RANGE, "executable link",
                manifest([entry("a", kind=3, flags=1, byte_count=1)])),
        Fixture("malformed/manifest_empty_path.bin", "transfer", e, INVALID_STRING, "empty path",
                manifest([entry("")])),
        Fixture("malformed/manifest_absolute_path.bin", "transfer", e, INVALID_STRING, "path starts with '/'",
                manifest([entry("/etc/hosts")])),
        Fixture("malformed/manifest_trailing_slash.bin", "transfer", e, INVALID_STRING, "path ends with '/'",
                manifest([entry("a/", kind=2)])),
        Fixture("malformed/manifest_double_slash.bin", "transfer", e, INVALID_STRING, "empty component",
                manifest([entry("a//b")])),
        Fixture("malformed/manifest_parent_component.bin", "transfer", e, INVALID_STRING, "'..' component",
                manifest([entry("a/../../b")])),
        Fixture("malformed/manifest_dot_component.bin", "transfer", e, INVALID_STRING, "'.' component",
                manifest([entry("./a")])),
        Fixture("malformed/manifest_path_1025.bin", "transfer", e, INVALID_STRING, "1025-byte path",
                manifest([entry("/".join(["a" * 255] * 4) + "/a")])),
        Fixture("malformed/manifest_component_256.bin", "transfer", e, INVALID_STRING, "256-byte component",
                manifest([entry("a/" + "b" * 256)])),
        Fixture("malformed/manifest_path_control.bin", "transfer", e, INVALID_STRING, "tab in a path",
                manifest([entry("a\tb")])),
        Fixture("malformed/manifest_path_invalid_utf8.bin", "transfer", e, INVALID_STRING, "path is not UTF-8",
                manifest([entry(b"a/\xfe")])),
        Fixture("malformed/content_request_id_0.bin", "transfer", e, VALUE_OUT_OF_RANGE, "request_id 0",
                content_request(request_id=0)),
        Fixture("malformed/content_request_length_0.bin", "transfer", e, VALUE_OUT_OF_RANGE, "length 0",
                content_request(length=0)),
        Fixture("malformed/content_request_text_entry.bin", "transfer", e, VALUE_OUT_OF_RANGE,
                "entry 1 of plain text", content_request(obj=clipboard_text, entry_index=1)),
        Fixture("malformed/content_request_kind_0.bin", "transfer", e, VALUE_OUT_OF_RANGE, "object_kind 0",
                content_request(obj=object_ref(kind=0))),
        Fixture("malformed/content_request_cut.bin", "transfer", e, TRUNCATED, "length cut short",
                content_request()[:-4]),
        Fixture("malformed/content_chunk_empty.bin", "transfer", e, VALUE_OUT_OF_RANGE, "no data",
                content_chunk(b"")),
        Fixture("malformed/content_chunk_65505.bin", "transfer", e, VALUE_OUT_OF_RANGE, "65505 bytes of data",
                content_chunk(bytes(MAX_CHUNK + 1))),
        Fixture("malformed/content_chunk_id_0.bin", "transfer", e, VALUE_OUT_OF_RANGE, "request_id 0",
                content_chunk(b"\x01", request_id=0)),
        Fixture("malformed/content_chunk_short.bin", "transfer", e, TRUNCATED, "data shorter than declared",
                content_chunk(bytes(10), declared_length=11)),
        Fixture("malformed/content_end_id_0.bin", "transfer", e, VALUE_OUT_OF_RANGE, "request_id 0",
                transfer(0x86, u32(0) + u64(1) + DIGEST)),
        Fixture("malformed/content_end_short_digest.bin", "transfer", e, TRUNCATED, "31-byte digest",
                transfer(0x86, u32(4) + u64(1) + DIGEST[:31])),
        Fixture("malformed/transfer_error_code_0.bin", "transfer", e, VALUE_OUT_OF_RANGE, "code 0",
                transfer(0x87, u32(4) + u16(0))),
        Fixture("malformed/transfer_cancel_id_0.bin", "transfer", e, VALUE_OUT_OF_RANGE, "request_id 0",
                transfer(0x88, u32(0))),
        Fixture("malformed/transfer_cancel_trailing.bin", "transfer", e, TRAILING_BYTES, "one extra byte",
                transfer(0x88, u32(4) + b"\x00")),
    ]


# --- Output ------------------------------------------------------------------------------------

def fixtures() -> list[Fixture]:
    entries = control_fixtures() + input_fixtures() + transfer_fixtures()
    names = [f.name for f in entries]
    duplicates = {name for name in names if names.count(name) > 1}
    if duplicates:
        raise SystemExit(f"duplicate fixture names: {sorted(duplicates)}")
    limits = {"control": MAX_CONTROL, "input": MAX_INPUT_PLAINTEXT, "transfer": MAX_TRANSFER}
    for f in entries:
        if f.outcome == "valid" and len(f.data) > limits[f.kind]:
            raise SystemExit(f"{f.name}: a valid fixture is larger than its limit")
        if f.outcome == "error" and f.detail == TOO_LARGE and len(f.data) != limits[f.kind] + 1:
            raise SystemExit(f"{f.name}: a TOO_LARGE fixture must be one byte over the limit")
    return entries


def manifest_text(entries: list[Fixture]) -> str:
    lines = [
        "# Generated by protocol/tools/generate_fixtures.py from docs/PROTOCOL.md. Do not edit.",
        "# file\tkind\toutcome\ttype-or-error\tdescription",
    ]
    lines += [f"{f.name}\t{f.kind}\t{f.outcome}\t{f.detail}\t{f.description}" for f in entries]
    return "\n".join(lines) + "\n"


def main() -> int:
    check = sys.argv[1:] == ["--check"]
    entries = fixtures()
    expected = {FIXTURES / f.name: f.data for f in entries}
    expected[FIXTURES / "manifest.tsv"] = manifest_text(entries).encode()

    if check:
        stale = [str(path.relative_to(ROOT.parent)) for path, data in expected.items()
                 if not path.exists() or path.read_bytes() != data]
        on_disk = {p for p in FIXTURES.rglob("*") if p.is_file()}
        extra = [str(p.relative_to(ROOT.parent)) for p in sorted(on_disk - set(expected))]
        for path in stale:
            print(f"out of date: {path}", file=sys.stderr)
        for path in extra:
            print(f"not generated: {path}", file=sys.stderr)
        if stale or extra:
            print("Run protocol/tools/generate_fixtures.py", file=sys.stderr)
            return 1
        print(f"Fixtures up to date ({len(entries)} files).")
        return 0

    for path, data in expected.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    print(f"Wrote {len(entries)} fixtures and the manifest.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
