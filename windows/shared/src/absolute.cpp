#include <osdrop/win/absolute.hpp>

#include <algorithm>

namespace osdrop::win {
namespace {

constexpr std::int64_t kScale = 65536;
constexpr std::int32_t kMaxValue = 65535;

} // namespace

std::int32_t normalized_axis(std::int32_t pixel, std::int32_t origin, std::int32_t size) {
    const std::int64_t span = std::max(size, 1);
    const std::int64_t offset = std::clamp<std::int64_t>(std::int64_t{pixel} - origin, 0, span - 1);
    const std::int64_t value = ((offset * kScale) + span - 1) / span; // rounded up
    return static_cast<std::int32_t>(std::min<std::int64_t>(value, kMaxValue));
}

std::int32_t pixel_of(std::int32_t value, std::int32_t origin, std::int32_t size) {
    return static_cast<std::int32_t>(origin + ((std::int64_t{value} * std::max(size, 1)) / kScale));
}

Normalized normalized(const VirtualDesktop& desktop, std::int32_t x, std::int32_t y) {
    return {.x = normalized_axis(x, desktop.left, desktop.width),
            .y = normalized_axis(y, desktop.top, desktop.height)};
}

} // namespace osdrop::win
