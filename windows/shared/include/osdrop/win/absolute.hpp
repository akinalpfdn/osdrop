#pragma once

// SendInput's absolute pointer coordinates (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK): 0 to
// 65535 across the whole virtual desktop, which Windows turns back into a pixel as
// left + value * width / 65536. Rounding the other way up makes every pixel reachable exactly.

#include <cstdint>

namespace osdrop::win {

/// The virtual desktop in physical pixels (SM_XVIRTUALSCREEN ... SM_CYVIRTUALSCREEN).
struct VirtualDesktop {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

struct Normalized {
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool operator==(const Normalized&) const = default;
};

/// The absolute value of one axis that lands on `pixel`; pixels outside the desktop are clamped
/// to its edges. `size` is at least 1.
[[nodiscard]] std::int32_t normalized_axis(std::int32_t pixel, std::int32_t origin,
                                           std::int32_t size);
/// The pixel Windows moves the pointer to for an absolute value (the inverse, for tests).
[[nodiscard]] std::int32_t pixel_of(std::int32_t value, std::int32_t origin, std::int32_t size);

[[nodiscard]] Normalized normalized(const VirtualDesktop& desktop, std::int32_t x, std::int32_t y);

} // namespace osdrop::win
