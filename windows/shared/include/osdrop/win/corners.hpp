#pragma once

// A phone's corner on this PC (DEVPLAN Phase 21): the spot in a monitor's corner that takes a drag
// for the phone, and where the phone's box sits once it shows. Physical pixels; portable and unit
// tested.

#include <osdrop/osdrop.h>
#include <osdrop/win/screens.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace osdrop::win {

enum class ScreenCorner : std::uint8_t {
    TopLeft = 0,
    TopRight = 1,
    BottomLeft = 2,
    BottomRight = 3,
};

using MonitorKey = std::array<std::uint8_t, OSDROP_MONITOR_KEY_SIZE>;

/// The monitor a corner is on: the one with `key`, else the primary one, else the first; nullptr
/// when there is none.
[[nodiscard]] const PhysicalMonitor* corner_monitor(std::span<const PhysicalMonitor> monitors,
                                                    const MonitorKey& key);

/// A `width` by `height` rectangle in `area`'s `corner`, `inset` pixels in from both of its sides.
[[nodiscard]] PixelRect corner_rect(PixelRect area, ScreenCorner corner, std::int32_t width,
                                    std::int32_t height, std::int32_t inset);

/// `dips` device-independent pixels (96 per inch) on a monitor of `dpi`, in physical pixels.
[[nodiscard]] std::int32_t physical_pixels(std::int32_t dips, std::uint32_t dpi);

/// The monitor's whole rectangle.
[[nodiscard]] PixelRect monitor_rect(const PhysicalMonitor& monitor);

/// Where a box sits: the work area, or the whole monitor when the work area is unknown.
[[nodiscard]] PixelRect work_rect(const PhysicalMonitor& monitor);

/// The smallest rectangle holding both.
[[nodiscard]] PixelRect united(PixelRect first, PixelRect second);

} // namespace osdrop::win
