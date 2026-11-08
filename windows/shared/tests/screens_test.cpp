#include <osdrop/win/screens.hpp>

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace {

using osdrop::win::PhysicalMonitor;

TEST(WindowsScreens, AMixedScaleDesktopBecomesOneLogicalArrangement) {
    // A 150% laptop panel with a 100% monitor on its right, 300 pixels lower.
    const std::array monitors{PhysicalMonitor{.key = osdrop::win::monitor_key("panel"),
                                              .left = 0,
                                              .top = 0,
                                              .right = 1920,
                                              .bottom = 1200,
                                              .dpi = 144,
                                              .primary = true},
                              PhysicalMonitor{.key = osdrop::win::monitor_key("external"),
                                              .left = 1920,
                                              .top = 300,
                                              .right = 4480,
                                              .bottom = 1740,
                                              .dpi = 96,
                                              .primary = false}};
    const auto report = osdrop::win::screen_report(monitors);
    ASSERT_TRUE(report.has_value());
    const auto& panel = report.value().at(0);
    const auto& external = report.value().at(1);
    EXPECT_EQ(panel.native.width, 1920 * 16);
    EXPECT_EQ(panel.frame.width, 1280 * 16);
    EXPECT_EQ(external.native.x, 1920 * 16);
    EXPECT_EQ(external.frame.x, 1280 * 16) << "touches the panel";
    EXPECT_EQ(external.frame.y, 200 * 16) << "300 panel pixels are 200 points";
    EXPECT_EQ(panel.primary, 1);
    EXPECT_EQ(external.pixel_width, 2560);
}

TEST(WindowsScreens, ReportsOnlyWhatAScreenReportCarries) {
    const std::array empty{PhysicalMonitor{
        .key = {}, .left = 0, .top = 0, .right = 0, .bottom = 10, .dpi = 96, .primary = true}};
    EXPECT_FALSE(osdrop::win::screen_report(empty).has_value());
    const std::array no_primary{PhysicalMonitor{
        .key = {}, .left = 0, .top = 0, .right = 100, .bottom = 100, .dpi = 96, .primary = false}};
    EXPECT_FALSE(osdrop::win::screen_report(no_primary).has_value());
    EXPECT_NE(osdrop::win::monitor_key("a"), osdrop::win::monitor_key("b"));
}

TEST(WindowsScreens, DeviceNamesFitTheWire) {
    EXPECT_EQ(osdrop::win::device_name("DESKTOP-1234"), "DESKTOP-1234");
    EXPECT_EQ(osdrop::win::device_name("Çalışma\x07PC"), "ÇalışmaPC");
    EXPECT_EQ(osdrop::win::device_name(std::string(40, 'x') + std::string(30, 'y')).size(), 64U);
    std::string turkish;
    for (int i = 0; i < 40; ++i) {
        turkish += "ğ";
    }
    EXPECT_EQ(osdrop::win::device_name(turkish).size(), 64U) << "whole characters only";
    EXPECT_EQ(osdrop::win::device_name("   "), "PC");
}

} // namespace
