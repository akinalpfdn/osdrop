// Scrolling between the protocol's content movement and Windows' wheel units: directions, notches,
// fine trackpad steps and the fractions carried between events.
#include <osdrop/win/wheel.hpp>

#include <gtest/gtest.h>

namespace {

using osdrop::win::kPointsPerNotch;
using osdrop::win::kScrollScale;
using osdrop::win::kWheelDelta;
using osdrop::win::LineScroll;
using osdrop::win::scroll_of;
using osdrop::win::ScrollUnit;
using osdrop::win::Wheel;
using osdrop::win::WheelFromScroll;

TEST(Wheel, ALineIsANotchAndDirectionsFollowTheContent) {
    WheelFromScroll wheel;
    EXPECT_EQ(wheel.convert(ScrollUnit::Lines, 0, kScrollScale),
              (Wheel{.vertical = kWheelDelta, .horizontal = 0}))
        << "content moving down is scrolling up";
    EXPECT_EQ(wheel.convert(ScrollUnit::Lines, kScrollScale, 0),
              (Wheel{.vertical = 0, .horizontal = -kWheelDelta}))
        << "content moving right is scrolling left";
    EXPECT_EQ(wheel.convert(ScrollUnit::Lines, 0, -3 * kScrollScale),
              (Wheel{.vertical = -3 * kWheelDelta, .horizontal = 0}));
}

TEST(Wheel, TrackpadPointsKeepFineStepsAndCarryFractions) {
    WheelFromScroll wheel;
    EXPECT_EQ(wheel.convert(ScrollUnit::Points, 0, kPointsPerNotch * kScrollScale).vertical,
              kWheelDelta)
        << "a notch of points";
    // About a tenth of a point at a time: nothing is lost over many events.
    constexpr std::int32_t kStep = kScrollScale / 10;
    constexpr int kEvents = 4'800;
    std::int64_t total = 0;
    for (int i = 0; i < kEvents; ++i) {
        total += wheel.convert(ScrollUnit::Points, 0, kStep).vertical;
    }
    EXPECT_EQ(total,
              std::int64_t{kEvents} * kStep * kWheelDelta / (kScrollScale * kPointsPerNotch));
}

TEST(Wheel, CapturedWheelsBecomeLines) {
    EXPECT_EQ(scroll_of({.vertical = kWheelDelta, .horizontal = 0}),
              (LineScroll{.dx = 0, .dy = kScrollScale}));
    EXPECT_EQ(scroll_of({.vertical = 0, .horizontal = kWheelDelta}),
              (LineScroll{.dx = -kScrollScale, .dy = 0}));
    EXPECT_EQ(scroll_of({.vertical = 30, .horizontal = 0}), (LineScroll{.dx = 0, .dy = 64}))
        << "a high-resolution wheel's quarter notch";
}

TEST(Wheel, ANotchRoundTrips) {
    WheelFromScroll wheel;
    const LineScroll captured =
        scroll_of({.vertical = -kWheelDelta, .horizontal = 2 * kWheelDelta});
    EXPECT_EQ(wheel.convert(ScrollUnit::Lines, captured.dx, captured.dy),
              (Wheel{.vertical = -kWheelDelta, .horizontal = 2 * kWheelDelta}));
}

} // namespace
