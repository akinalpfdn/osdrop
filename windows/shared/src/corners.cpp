#include <osdrop/win/corners.hpp>

#include <algorithm>

namespace osdrop::win {

const PhysicalMonitor* corner_monitor(std::span<const PhysicalMonitor> monitors,
                                      const MonitorKey& key) {
    if (monitors.empty()) {
        return nullptr;
    }
    const auto chosen =
        std::ranges::find_if(monitors, [&](const PhysicalMonitor& m) { return m.key == key; });
    if (chosen != monitors.end()) {
        return &*chosen;
    }
    const auto primary =
        std::ranges::find_if(monitors, [](const PhysicalMonitor& m) { return m.primary; });
    return primary != monitors.end() ? &*primary : monitors.data();
}

PixelRect corner_rect(PixelRect area, ScreenCorner corner, std::int32_t width, std::int32_t height,
                      std::int32_t inset) {
    const bool left = corner == ScreenCorner::TopLeft || corner == ScreenCorner::BottomLeft;
    const bool top = corner == ScreenCorner::TopLeft || corner == ScreenCorner::TopRight;
    const std::int32_t x = left ? area.left + inset : area.right - inset - width;
    const std::int32_t y = top ? area.top + inset : area.bottom - inset - height;
    return {.left = x, .top = y, .right = x + width, .bottom = y + height};
}

std::int32_t physical_pixels(std::int32_t dips, std::uint32_t dpi) {
    constexpr std::int64_t kDefaultDpi = 96;
    const std::int64_t scaled = static_cast<std::int64_t>(dips) * (dpi == 0 ? kDefaultDpi : dpi);
    return static_cast<std::int32_t>((scaled + (kDefaultDpi / 2)) / kDefaultDpi);
}

PixelRect monitor_rect(const PhysicalMonitor& monitor) {
    return {
        .left = monitor.left, .top = monitor.top, .right = monitor.right, .bottom = monitor.bottom};
}

PixelRect work_rect(const PhysicalMonitor& monitor) {
    const PixelRect& work = monitor.work;
    return work.right > work.left && work.bottom > work.top ? work : monitor_rect(monitor);
}

PixelRect united(PixelRect first, PixelRect second) {
    return {.left = std::min(first.left, second.left),
            .top = std::min(first.top, second.top),
            .right = std::max(first.right, second.right),
            .bottom = std::max(first.bottom, second.bottom)};
}

} // namespace osdrop::win
