// SendInput's absolute coordinates: every pixel of the virtual desktop is reached exactly, for
// desktops that start left of and above the primary monitor too.
#include <osdrop/win/absolute.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace {

using osdrop::win::normalized;
using osdrop::win::normalized_axis;
using osdrop::win::pixel_of;
using osdrop::win::VirtualDesktop;

TEST(Absolute, EveryPixelIsReachedExactly) {
    for (const std::int32_t size :
         std::array{1, 2, 3, 640, 1080, 1366, 1920, 2560, 3840, 7680, 15360}) {
        for (const std::int32_t origin : std::array{0, -1920, -2560, 1440}) {
            for (std::int32_t pixel = origin; pixel < origin + size; ++pixel) {
                const std::int32_t value = normalized_axis(pixel, origin, size);
                ASSERT_GE(value, 0);
                ASSERT_LE(value, 65535);
                ASSERT_EQ(pixel_of(value, origin, size), pixel)
                    << "size " << size << " origin " << origin;
            }
        }
    }
}

TEST(Absolute, PixelsOutsideTheDesktopAreClampedToItsEdges) {
    EXPECT_EQ(normalized_axis(-5000, -1920, 3840), 0);
    EXPECT_EQ(pixel_of(normalized_axis(9000, -1920, 3840), -1920, 3840), 1919);
}

TEST(Absolute, BothAxesFollowTheDesktop) {
    const VirtualDesktop desktop{.left = -2560, .top = -360, .width = 4480, .height = 1440};
    const auto value = normalized(desktop, 0, 0); // the primary monitor's corner
    EXPECT_EQ(pixel_of(value.x, desktop.left, desktop.width), 0);
    EXPECT_EQ(pixel_of(value.y, desktop.top, desktop.height), 0);
}

} // namespace
