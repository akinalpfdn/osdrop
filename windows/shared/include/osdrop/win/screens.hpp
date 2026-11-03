#pragma once

// This PC's monitors as core takes them (docs/PROTOCOL.md "ScreenReport"): native rectangles in
// physical pixels, logical sizes from each monitor's scale, and frames that core derives so
// monitors touching in pixels touch in the arrangement. Portable and unit tested.

#include <osdrop/osdrop.h>
#include <osdrop/win/expected.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace osdrop::win {

/// One monitor as Windows reports it to a per-monitor DPI aware thread.
struct PhysicalMonitor {
    std::array<std::uint8_t, OSDROP_MONITOR_KEY_SIZE> key{};
    std::int32_t left = 0; ///< physical pixels, virtual screen coordinates
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
    std::uint32_t dpi = 96; ///< effective DPI; 96 is 100%
    bool primary = false;
};

/// A stable 16-byte key for a monitor from its device interface path (FNV-1a, two seeds).
[[nodiscard]] std::array<std::uint8_t, OSDROP_MONITOR_KEY_SIZE> monitor_key(std::string_view path);

/// The monitors for osdrop_mesh_set_screens; fails for monitors a ScreenReport cannot carry.
[[nodiscard]] Expected<std::vector<osdrop_monitor>, osdrop_result>
screen_report(std::span<const PhysicalMonitor> monitors);

/// The computer name other computers show: control characters dropped, cut to the 64 bytes a
/// name may have on the wire at a character boundary, "PC" when nothing is left.
[[nodiscard]] std::string device_name(std::string_view computer_name);

} // namespace osdrop::win
