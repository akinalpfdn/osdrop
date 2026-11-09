// This PC's own input as core takes it (DEVPLAN Phase 11): positions kept on a monitor while the
// push still moves, the park point while captured, click counts, key repeats and scrolling.
#include <osdrop/win/forwarder.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <vector>

namespace {

namespace input = osdrop::win::input;
using osdrop::win::ClickSettings;
using osdrop::win::InputForwarder;
using osdrop::win::PhysicalMonitor;
using osdrop::win::Pixel;
using osdrop::win::ScanCode;
using Clock = InputForwarder::Clock;
using std::chrono::milliseconds;

constexpr std::int32_t kSixteenths = 16;

PhysicalMonitor monitor(std::int32_t left, std::int32_t top, std::int32_t right,
                        std::int32_t bottom) {
    return {.key = {},
            .left = left,
            .top = top,
            .right = right,
            .bottom = bottom,
            .dpi = 96,
            .primary = left == 0 && top == 0};
}

class InputForwarderTest : public ::testing::Test {
protected:
    InputForwarderTest() {
        // A main monitor and one to its left, lower down.
        forwarder.set_monitors({monitor(0, 0, 1920, 1080), monitor(-1280, 200, 0, 1224)});
    }

    osdrop_input convert(const input::Report& report) {
        const auto forwarded = forwarder.convert(report, now);
        EXPECT_TRUE(forwarded.has_value());
        return forwarded.value().input; // throws, failing the test, when there is none
    }

    osdrop_input press(input::Button button, bool down) {
        return convert(input::PhysicalPress{.button = button, .down = down});
    }

    Clock::time_point now = Clock::time_point(std::chrono::hours(2));
    InputForwarder forwarder{ClickSettings{.interval = milliseconds(500), .width = 4, .height = 4}};
};

TEST_F(InputForwarderTest, APushPastAnOuterEdgeStaysOnTheMonitorAndKeepsItsMovement) {
    const osdrop_input event =
        convert(input::WatchedMotion{.x = 1925, .y = 300, .dx = 6, .dy = 0, .ticks = 42});
    EXPECT_EQ(event.kind, OSDROP_INPUT_POINTER);
    EXPECT_EQ(event.x, 1919 * kSixteenths) << "Windows keeps the cursor on the last pixel";
    EXPECT_EQ(event.y, 300 * kSixteenths);
    EXPECT_EQ(event.dx, 6 * kSixteenths) << "the push still moves";
    EXPECT_EQ(forwarder.position(), (Pixel{.x = 1919, .y = 300}));
    const auto forwarded = forwarder.convert(input::WatchedMotion{.ticks = 42}, now);
    ASSERT_TRUE(forwarded.has_value());
    EXPECT_EQ(forwarded->ticks, 42U);
}

TEST_F(InputForwarderTest, ACursorBetweenMonitorsGoesToTheNearest) {
    convert(input::WatchedMotion{.x = -5, .y = 100, .dx = -5, .dy = 0});
    EXPECT_EQ(forwarder.position(), (Pixel{.x = 0, .y = 100})) << "above the left monitor's top";
    convert(input::WatchedMotion{.x = -700, .y = 1100, .dx = 0, .dy = 1});
    EXPECT_EQ(forwarder.position(), (Pixel{.x = -700, .y = 1100})) << "on the left monitor";
}

TEST_F(InputForwarderTest, WhileCapturedTheCursorWaitsAtThePark) {
    forwarder.capture({.x = 960, .y = 540});
    const osdrop_input event = convert(input::CapturedMotion{.dx = -3, .dy = 4, .ticks = 7});
    EXPECT_EQ(event.x, 960 * kSixteenths);
    EXPECT_EQ(event.y, 540 * kSixteenths);
    EXPECT_EQ(event.dx, -3 * kSixteenths);
    EXPECT_EQ(event.dy, 4 * kSixteenths);
    forwarder.release({.x = 1919, .y = 500});
    EXPECT_EQ(press(input::Button::Left, true).x, 1919 * kSixteenths);
}

TEST_F(InputForwarderTest, ClicksCountWithinTheIntervalAndTheRectangle) {
    convert(input::WatchedMotion{.x = 100, .y = 100});
    EXPECT_EQ(press(input::Button::Left, true).click_count, 1);
    EXPECT_EQ(press(input::Button::Left, false).click_count, 1);
    now += milliseconds(200);
    convert(input::WatchedMotion{.x = 102, .y = 98});
    EXPECT_EQ(press(input::Button::Left, true).click_count, 2);
    EXPECT_EQ(press(input::Button::Left, false).click_count, 2) << "the release carries it too";
    now += milliseconds(501);
    EXPECT_EQ(press(input::Button::Left, true).click_count, 1) << "too late";
    now += milliseconds(100);
    EXPECT_EQ(press(input::Button::Right, true).click_count, 1) << "another button";
    now += milliseconds(100);
    convert(input::WatchedMotion{.x = 105, .y = 98});
    EXPECT_EQ(press(input::Button::Right, true).click_count, 1) << "outside the rectangle";
}

TEST_F(InputForwarderTest, CapturedClicksCountByWhereTheyLandOnTheOtherComputer) {
    forwarder.capture({.x = 960, .y = 540});
    EXPECT_EQ(press(input::Button::Left, true).click_count, 1);
    press(input::Button::Left, false);
    now += milliseconds(150);
    convert(input::CapturedMotion{.dx = 40, .dy = 0});
    EXPECT_EQ(press(input::Button::Left, true).click_count, 1)
        << "the cursor moved on there, though it waits here";
    press(input::Button::Left, false);
    now += milliseconds(150);
    EXPECT_EQ(press(input::Button::Left, true).click_count, 2);
}

TEST_F(InputForwarderTest, AKeyDownAgainWhileDownIsARepeat) {
    const ScanCode a{.code = 0x1E, .extended = false};
    const osdrop_input down = convert(input::PhysicalKey{.scan = a, .down = true});
    EXPECT_EQ(down.kind, OSDROP_INPUT_KEY);
    EXPECT_EQ(down.page, 7);
    EXPECT_EQ(down.usage, 0x04);
    EXPECT_EQ(down.action, 1);
    EXPECT_EQ(convert(input::PhysicalKey{.scan = a, .down = true}).action, 3);
    EXPECT_EQ(convert(input::PhysicalKey{.scan = a, .down = false}).action, 2);
    EXPECT_EQ(convert(input::PhysicalKey{.scan = a, .down = true}).action, 1);
    forwarder.reset();
    EXPECT_EQ(convert(input::PhysicalKey{.scan = a, .down = true}).action, 1)
        << "held keys are forgotten after a reconnect";
    EXPECT_FALSE(forwarder
                     .convert(input::PhysicalKey{.scan = ScanCode{.code = 0x7F, .extended = true},
                                                 .down = true},
                              now)
                     .has_value())
        << "no key has it";
}

TEST_F(InputForwarderTest, AWheelNotchIsALineOfContentMovement) {
    convert(input::WatchedMotion{.x = 10, .y = 20});
    const osdrop_input up = convert(input::PhysicalScroll{.vertical = 120, .horizontal = 0});
    EXPECT_EQ(up.kind, OSDROP_INPUT_SCROLL);
    EXPECT_EQ(up.scroll_unit, 1);
    EXPECT_EQ(up.dy, 256) << "scrolling up moves content down";
    EXPECT_EQ(up.x, 10 * kSixteenths);
    const osdrop_input right = convert(input::PhysicalScroll{.vertical = 0, .horizontal = 60});
    EXPECT_EQ(right.dx, -128) << "scrolling right moves content left";
}

TEST_F(InputForwarderTest, OtherReportsAreNoInput) {
    EXPECT_FALSE(forwarder.convert(input::LocalInput{}, now).has_value());
    EXPECT_FALSE(forwarder.convert(input::Released{}, now).has_value());
    EXPECT_FALSE(forwarder.convert(input::HotkeyPressed{}, now).has_value());
}

TEST_F(InputForwarderTest, PushingAtTheEdgeOfAGamesRectangleMovesNothing) {
    // A full-screen game on the main monitor holds the cursor there.
    forwarder.set_clip(osdrop::win::PixelRect{.left = 0, .top = 0, .right = 1920, .bottom = 1080});
    osdrop_input event = convert(input::WatchedMotion{.x = 1925, .y = 300, .dx = 6, .dy = 2});
    EXPECT_EQ(event.x, 1919 * kSixteenths);
    EXPECT_EQ(event.dx, 0) << "no push past the edge";
    EXPECT_EQ(event.dy, 2 * kSixteenths) << "along the edge it still moves";
    event = convert(input::WatchedMotion{.x = 1910, .y = 300, .dx = -4, .dy = 0});
    EXPECT_EQ(event.dx, -4 * kSixteenths) << "inside it moves as before";

    forwarder.set_clip(std::nullopt);
    event = convert(input::WatchedMotion{.x = 1925, .y = 300, .dx = 6, .dy = 0});
    EXPECT_EQ(event.dx, 6 * kSixteenths) << "let go, the push crosses again";
    forwarder.set_clip(osdrop::win::PixelRect{});
    event = convert(input::WatchedMotion{.x = 1925, .y = 300, .dx = 6, .dy = 0});
    EXPECT_EQ(event.dx, 6 * kSixteenths) << "an empty rectangle holds nothing";
}

} // namespace
