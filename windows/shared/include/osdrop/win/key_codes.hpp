#pragma once

// Keys between USB HID usages, as they travel between computers, and Windows scan codes (set 1),
// as low-level keyboard hooks report them and SendInput takes them with KEYEVENTF_SCANCODE. A scan
// code names a key position, so the computer the key arrives on applies its own layout (DEVPLAN
// Phase 12). Extended keys carry the E0 prefix: LLKHF_EXTENDED on capture, KEYEVENTF_EXTENDEDKEY
// on injection.
//
// Three keys share a scan code with another usage and come back as that one: the non-US # key
// (HID 0x32) is the backslash position (0x31), and the keyboard page's mute and volume keys
// (0x7F-0x81) arrive as the consumer page's (0xE2, 0xE9, 0xEA), which Macs send. Num Lock and
// Pause both have scan code 0x45; hooks report Num Lock as extended and Pause (whose real prefix is
// E1 1D) as not, which this table follows.

#include <compare>
#include <cstdint>
#include <optional>
#include <span>

namespace osdrop::win {

struct ScanCode {
    std::uint16_t code = 0;
    bool extended = false;
    auto operator<=>(const ScanCode&) const = default;
};

struct HidUsage {
    std::uint16_t page = 0; ///< 7 keyboard, 12 consumer
    std::uint16_t usage = 0;
    auto operator<=>(const HidUsage&) const = default;
};

struct KeyCode {
    HidUsage usage;
    ScanCode scan;
};

/// The scan code of a usage; nullopt for keys Windows has no scan code for.
[[nodiscard]] std::optional<ScanCode> scan_code_of(HidUsage usage);
/// The usage of a scan code; nullopt for scan codes no key in the table has.
[[nodiscard]] std::optional<HidUsage> usage_of(ScanCode scan);
/// Every entry, for tests and the documented matrix.
[[nodiscard]] std::span<const KeyCode> key_codes();

} // namespace osdrop::win
