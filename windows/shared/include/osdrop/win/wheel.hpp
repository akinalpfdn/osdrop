#pragma once

// Scrolling between the protocol and Windows' wheel units (DEVPLAN Phase 12; docs/PROTOCOL.md,
// "0x25 ScrollEvent"). The protocol carries content movement: positive dx moves content right,
// positive dy moves it down, in 1/256 of a line (notched wheels) or a point (trackpads, continuous
// wheels). Windows takes WHEEL_DELTA (120) per notch: a positive vertical wheel scrolls up, which
// moves content down, and a positive horizontal wheel scrolls right, which moves content left.
//
// A line is one notch on either side, so each computer scrolls a notch the way its own settings
// say. A point is 1/48 of a notch, about what a notch scrolls in Windows apps at three lines;
// trackpad scrolling keeps its fine steps, because SendInput takes deltas below 120. Fractions
// below one wheel unit are carried to the next event, so slow scrolling is never lost.

#include <cstdint>

namespace osdrop::win {

inline constexpr std::int32_t kWheelDelta = 120;
/// Points of trackpad scrolling per notch.
inline constexpr std::int32_t kPointsPerNotch = 48;
/// The protocol's scroll fraction: 1/256 of a unit.
inline constexpr std::int32_t kScrollScale = 256;

/// Wheel units as SendInput and low-level mouse hooks use them.
struct Wheel {
    std::int32_t vertical = 0;   ///< positive: scroll up
    std::int32_t horizontal = 0; ///< positive: scroll right
    bool operator==(const Wheel&) const = default;
};

enum class ScrollUnit : std::uint8_t { Lines = 1, Points = 2 };

/// Protocol scrolling into wheel units, one event at a time, carrying fractions.
class WheelFromScroll {
public:
    [[nodiscard]] Wheel convert(ScrollUnit unit, std::int32_t dx, std::int32_t dy);

private:
    std::int64_t vertical_rest_ = 0; ///< in 1/(256 * points per notch) of a wheel unit
    std::int64_t horizontal_rest_ = 0;
};

/// The protocol's scroll for a captured wheel event: lines, positive dy moving content down.
struct LineScroll {
    std::int32_t dx = 0; ///< 1/256 of a line
    std::int32_t dy = 0;
    bool operator==(const LineScroll&) const = default;
};

[[nodiscard]] LineScroll scroll_of(Wheel wheel);

} // namespace osdrop::win
