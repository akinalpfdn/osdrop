// One computer's monitors: conversions between native and logical units, cursor movement through
// a computer's own monitors as its system moves it, outer edges, and the logical frames derived
// for a Windows desktop with mixed scales.
#include "layout/screens.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <vector>

namespace {

using osdrop::layout::Edge;
using osdrop::layout::kUnit;
using osdrop::layout::Monitor;
using osdrop::layout::Point;
using osdrop::layout::Rect;
using osdrop::layout::Screens;

Rect units(std::int64_t x, std::int64_t y, std::int64_t width, std::int64_t height) {
    return {.x = x * kUnit, .y = y * kUnit, .width = width * kUnit, .height = height * kUnit};
}

Point at(std::int64_t x, std::int64_t y) {
    return {.x = x * kUnit, .y = y * kUnit};
}

/// A monitor whose native units are its logical ones, as on macOS.
Monitor mac(std::uint8_t key, Rect frame, bool primary = false) {
    return {.key = {key},
            .frame = frame,
            .native = frame,
            .pixel_width = 1,
            .pixel_height = 1,
            .primary = primary};
}

/// A Windows monitor: native in physical pixels, logical size at `percent` scale.
Monitor windows(std::uint8_t key, Rect native, std::int64_t percent, bool primary = false) {
    return {.key = {key},
            .frame = {.x = 0,
                      .y = 0,
                      .width = native.width * 100 / percent,
                      .height = native.height * 100 / percent},
            .native = native,
            .pixel_width = 1,
            .pixel_height = 1,
            .primary = primary};
}

TEST(LayoutScreens, ConvertsBetweenNativeAndLogicalUnits) {
    const Monitor m{.key = {1},
                    .frame = units(100, 50, 1280, 800),
                    .native = units(1920, 0, 1920, 1200),
                    .pixel_width = 1920,
                    .pixel_height = 1200,
                    .primary = false};
    EXPECT_EQ(m.to_logical(at(1920, 0)), at(100, 50));
    EXPECT_EQ(m.to_logical(at(1920 + 960, 600)), at(100 + 640, 50 + 400));
    EXPECT_EQ(m.to_native(at(100 + 640, 50 + 400)), at(1920 + 960, 600));
    for (std::int64_t x = 0; x < 1920 * kUnit; x += 37) {
        const Point native{.x = (1920 * kUnit) + x, .y = 0};
        EXPECT_LE(std::abs(m.to_native(m.to_logical(native)).x - native.x), 1);
    }
}

TEST(LayoutScreens, MovesWithinAndAcrossTouchingMonitors) {
    // An external monitor on the left of the built-in one, its top 240 points higher.
    const Screens screens(
        {mac(1, units(0, 0, 1470, 956), true), mac(2, units(-2560, -240, 2560, 1440))});
    const auto inside = screens.move(at(100, 100), at(50, 20));
    EXPECT_EQ(inside.position, at(150, 120));
    EXPECT_FALSE(inside.blocked_x || inside.blocked_y);

    const auto across = screens.move(at(10, 500), at(-30, 0));
    EXPECT_EQ(across.position, at(-20, 500));
    EXPECT_EQ(across.monitor, 1U);

    const auto wall = screens.move(at(1460, 500), at(30, 0));
    EXPECT_EQ(wall.position, (Point{.x = (1470 * kUnit) - 1, .y = 500 * kUnit}));
    EXPECT_EQ(wall.blocked_x, Edge::Right);
    EXPECT_EQ(wall.overshoot_x, 20 * kUnit + 1);

    // Up and left from the built-in display's top-left corner: left into the external monitor,
    // then up within it, as macOS moves the cursor.
    const auto corner = screens.move(at(5, 5), at(-20, -100));
    EXPECT_EQ(corner.position, at(-15, -95));
    EXPECT_EQ(corner.monitor, 1U);
    const auto above = screens.move(at(5, 5), at(0, -10));
    EXPECT_EQ(above.blocked_y, Edge::Top) << "nothing above the built-in display";
}

TEST(LayoutScreens, OuterEdgesAreThoseNoOwnMonitorContinues) {
    const Screens screens(
        {mac(1, units(0, 0, 1000, 1000), true), mac(2, units(1000, 0, 1000, 500))});
    EXPECT_FALSE(screens.at_outer_edge(0, Edge::Right, at(999, 100))) << "monitor 2 continues";
    EXPECT_TRUE(screens.at_outer_edge(0, Edge::Right, at(999, 700))) << "below monitor 2";
    EXPECT_FALSE(screens.at_outer_edge(0, Edge::Right, at(990, 700))) << "not at the edge";
    EXPECT_TRUE(screens.at_outer_edge(1, Edge::Bottom, at(1500, 499)));
    EXPECT_TRUE(screens.at_outer_edge(0, Edge::Left, at(0, 10)));
}

TEST(LayoutScreens, MovementNeverLeavesTheMonitors) {
    std::mt19937 random(7);
    for (int layout = 0; layout < 200; ++layout) {
        // Two or three monitors in a row or an L, touching, of random sizes.
        std::vector<Monitor> monitors{
            mac(1, units(0, 0, 800 + (random() % 2000), 600 + (random() % 1200)), true)};
        const Rect first = monitors[0].frame;
        monitors.push_back(mac(2, {.x = first.right(),
                                   .y = first.y + static_cast<std::int64_t>(random() % 400) * kUnit,
                                   .width = (500 + (random() % 2000)) * kUnit,
                                   .height = (400 + (random() % 1200)) * kUnit}));
        if (random() % 2 == 0) {
            monitors.push_back(mac(
                3,
                {.x = first.x, .y = first.bottom(), .width = first.width, .height = 900 * kUnit}));
        }
        const Screens screens(monitors);
        Point cursor = at(10, 10);
        for (int step = 0; step < 300; ++step) {
            const Point delta{.x = (static_cast<std::int64_t>(random() % 4001) - 2000) * 8,
                              .y = (static_cast<std::int64_t>(random() % 4001) - 2000) * 8};
            const auto moved = screens.move(cursor, delta);
            ASSERT_TRUE(screens.monitor_at(moved.position).has_value());
            ASSERT_EQ(screens.monitor_at(moved.position), moved.monitor);
            cursor = moved.position;
        }
    }
}

TEST(LayoutScreens, WindowsFramesKeepMonitorsTouchingWhereTheyMeet) {
    // A 150% laptop panel with a 100% monitor on its right, 300 pixels lower, and a 125% monitor
    // above the laptop.
    const auto frames =
        osdrop::layout::derive_frames({windows(1, units(0, 0, 1920, 1200), 150, true),
                                       windows(2, units(1920, 300, 2560, 1440), 100),
                                       windows(3, units(-200, -1600, 2560, 1600), 125)});
    EXPECT_EQ(frames[0].frame, units(0, 0, 1280, 800));
    EXPECT_EQ(frames[1].frame, units(1280, 200, 2560, 1440)) << "300 laptop pixels are 200 points";
    EXPECT_EQ(frames[2].frame.bottom(), 0) << "touches the laptop's top";
    EXPECT_EQ(frames[2].frame.x, osdrop::layout::scale(-200 * kUnit, 1280, 1920))
        << "200 laptop pixels to the left";
    EXPECT_EQ(frames[2].frame.width, 2048 * kUnit);
    EXPECT_EQ(frames[2].frame.height, 1280 * kUnit);
}

} // namespace
