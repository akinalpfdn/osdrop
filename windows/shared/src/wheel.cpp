#include <osdrop/win/wheel.hpp>

namespace osdrop::win {

namespace {

/// Wheel units are counted in this fraction, the protocol's 1/256 of a point.
constexpr std::int64_t kDenominator = std::int64_t{kScrollScale} * kPointsPerNotch;

/// Whole wheel units of `rest` plus `amount`, keeping the fraction in `rest`.
std::int32_t take(std::int64_t& rest, std::int64_t amount) {
    rest += amount;
    const std::int64_t whole = rest / kDenominator; // toward zero
    rest -= whole * kDenominator;
    return static_cast<std::int32_t>(whole);
}

} // namespace

Wheel WheelFromScroll::convert(ScrollUnit unit, std::int32_t dx, std::int32_t dy) {
    const std::int64_t per_unit =
        unit == ScrollUnit::Lines ? std::int64_t{kWheelDelta} * kPointsPerNotch : kWheelDelta;
    // Content moving down is scrolling up; content moving right is scrolling left.
    return {.vertical = take(vertical_rest_, dy * per_unit),
            .horizontal = take(horizontal_rest_, -std::int64_t{dx} * per_unit)};
}

LineScroll scroll_of(Wheel wheel) {
    const auto lines = [](std::int64_t units) {
        return static_cast<std::int32_t>(units * kScrollScale / kWheelDelta);
    };
    return {.dx = lines(-std::int64_t{wheel.horizontal}), .dy = lines(wheel.vertical)};
}

} // namespace osdrop::win
