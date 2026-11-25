// A phone's corner on the PC (DEVPLAN Phase 21): which monitor it is on, where its hot spot and its
// box sit, in physical pixels.
#include <osdrop/win/corners.hpp>

#include <gtest/gtest.h>

#include <vector>

namespace {

using osdrop::win::corner_monitor;
using osdrop::win::corner_rect;
using osdrop::win::MonitorKey;
using osdrop::win::PhysicalMonitor;
using osdrop::win::PixelRect;
using osdrop::win::ScreenCorner;

PhysicalMonitor monitor(std::uint8_t id, PixelRect bounds, bool primary) {
    PhysicalMonitor m;
    m.key.fill(id);
    m.left = bounds.left;
    m.top = bounds.top;
    m.right = bounds.right;
    m.bottom = bounds.bottom;
    m.primary = primary;
    return m;
}

TEST(Corners, AStoredMonitorIsUsedWhileConnectedTheMainOneOtherwise) {
    const std::vector<PhysicalMonitor> monitors{
        monitor(1, {.left = -1920, .top = 0, .right = 0, .bottom = 1080}, false),
        monitor(2, {.left = 0, .top = 0, .right = 2560, .bottom = 1440}, true)};
    MonitorKey left{};
    left.fill(1);
    EXPECT_EQ(corner_monitor(monitors, left), monitors.data());
    MonitorKey gone{};
    gone.fill(9);
    EXPECT_EQ(corner_monitor(monitors, gone), &monitors[1]) << "the main monitor";
    EXPECT_EQ(corner_monitor({}, gone), nullptr);
}

TEST(Corners, RectanglesSitInTheirCornerInsetFromBothSides) {
    const PixelRect area{.left = 0, .top = 0, .right = 1920, .bottom = 1032};
    EXPECT_EQ(corner_rect(area, ScreenCorner::BottomRight, 300, 100, 8),
              (PixelRect{.left = 1612, .top = 924, .right = 1912, .bottom = 1024}));
    EXPECT_EQ(corner_rect(area, ScreenCorner::TopLeft, 300, 100, 8),
              (PixelRect{.left = 8, .top = 8, .right = 308, .bottom = 108}));
    EXPECT_EQ(corner_rect(area, ScreenCorner::TopRight, 64, 64, 0),
              (PixelRect{.left = 1856, .top = 0, .right = 1920, .bottom = 64}));
    EXPECT_EQ(corner_rect(area, ScreenCorner::BottomLeft, 64, 64, 0),
              (PixelRect{.left = 0, .top = 968, .right = 64, .bottom = 1032}));
}

TEST(Corners, SizesScaleWithTheMonitorsDpi) {
    EXPECT_EQ(osdrop::win::physical_pixels(64, 96), 64);
    EXPECT_EQ(osdrop::win::physical_pixels(64, 144), 96) << "150%";
    EXPECT_EQ(osdrop::win::physical_pixels(8, 120), 10) << "125%";
    EXPECT_EQ(osdrop::win::physical_pixels(8, 0), 8) << "an unknown DPI counts as 100%";
}

TEST(Corners, TheBoxSitsInTheWorkAreaWhenItIsKnown) {
    PhysicalMonitor m = monitor(1, {.left = 0, .top = 0, .right = 1920, .bottom = 1080}, true);
    EXPECT_EQ(osdrop::win::work_rect(m), osdrop::win::monitor_rect(m));
    m.work = {.left = 0, .top = 0, .right = 1920, .bottom = 1032};
    EXPECT_EQ(osdrop::win::work_rect(m), m.work);
}

TEST(Corners, TheTargetCoversTheBoxAndTheCorner) {
    const PixelRect box{.left = 1612, .top = 900, .right = 1912, .bottom = 1024};
    const PixelRect zone{.left = 1856, .top = 1016, .right = 1920, .bottom = 1080};
    EXPECT_EQ(osdrop::win::united(box, zone),
              (PixelRect{.left = 1612, .top = 900, .right = 1920, .bottom = 1080}));
}

} // namespace
