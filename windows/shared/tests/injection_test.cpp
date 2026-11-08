// Input from another computer turned into requests to OsdropInput (DEVPLAN Phase 3).
#include <osdrop/win/injection.hpp>

#include <gtest/gtest.h>

#include <variant>
#include <vector>

namespace {

namespace input = osdrop::win::input;
using osdrop::win::Injection;
using osdrop::win::ScanCode;

osdrop_input event(osdrop_input_kind kind) {
    osdrop_input value{};
    value.kind = kind;
    return value;
}

TEST(Injection, PositionsAreSixteenthsOfAPixelRoundedDown) {
    EXPECT_EQ(Injection::pixel(16 * 100 + 15), 100);
    EXPECT_EQ(Injection::pixel(-1), -1) << "a monitor left of the main one";
    EXPECT_EQ(Injection::pixel(-16 * 1920), -1920);
    EXPECT_EQ(Injection::pixel(INT32_MAX), input::kMaxCoordinate);
}

TEST(Injection, PointerMovesTheCursor) {
    Injection injection;
    osdrop_input moved = event(OSDROP_INPUT_POINTER);
    moved.x = 16 * 640;
    moved.y = 16 * -20;
    EXPECT_EQ(injection.requests(moved, false),
              (std::vector<input::Request>{input::Move{.x = 640, .y = -20}}));
}

TEST(Injection, ButtonsPressWhereTheyHappen) {
    Injection injection;
    osdrop_input press = event(OSDROP_INPUT_BUTTON);
    press.x = 16 * 10;
    press.y = 16 * 20;
    press.button = 1;
    press.action = 1;
    EXPECT_EQ(
        injection.requests(press, false),
        (std::vector<input::Request>{input::Move{.x = 10, .y = 20},
                                     input::Press{.button = input::Button::Right, .down = true}}));
    press.button = 9; // a button Windows has no input for
    EXPECT_TRUE(injection.requests(press, false).empty());
}

TEST(Injection, KeysGoByScanCodeAndRepeatsPressAgain) {
    Injection injection;
    osdrop_input key = event(OSDROP_INPUT_KEY);
    key.page = 7;
    key.usage = 0x04; // A
    key.action = 3;
    EXPECT_EQ(injection.requests(key, false),
              (std::vector<input::Request>{
                  input::Key{.scan = ScanCode{.code = 0x1E, .extended = false}, .down = true}}));
    key.usage = 0x4F; // right arrow: extended
    key.action = 2;
    EXPECT_EQ(injection.requests(key, false),
              (std::vector<input::Request>{
                  input::Key{.scan = ScanCode{.code = 0x4D, .extended = true}, .down = false}}));
    key.page = 9; // no such page
    EXPECT_TRUE(injection.requests(key, false).empty());
}

TEST(Injection, ScrollingTurnsIntoWheelUnitsAndCarriesFractions) {
    Injection injection;
    osdrop_input scroll = event(OSDROP_INPUT_SCROLL);
    scroll.x = 16 * 5;
    scroll.y = 16 * 6;
    scroll.scroll_unit = 1;
    scroll.dy = 256; // one line, content down: the wheel scrolls up
    EXPECT_EQ(injection.requests(scroll, false),
              (std::vector<input::Request>{input::Move{.x = 5, .y = 6},
                                           input::Scroll{.vertical = 120, .horizontal = 0}}));
    scroll.scroll_unit = 2;
    scroll.dy = 1; // 1/256 of a point: less than a wheel unit
    EXPECT_TRUE(injection.requests(scroll, false).empty());
}

TEST(Injection, CapsLockIsPressedOnlyWhenItDiffers) {
    Injection injection;
    osdrop_input caps = event(OSDROP_INPUT_CAPS_LOCK);
    caps.action = 1;
    EXPECT_TRUE(injection.requests(caps, true).empty());
    const auto requests = injection.requests(caps, false);
    ASSERT_EQ(requests.size(), 2U);
    EXPECT_TRUE(std::get<input::Key>(requests[0]).down);
    EXPECT_FALSE(std::get<input::Key>(requests[1]).down);
    caps.action = 2;
    EXPECT_EQ(injection.requests(caps, true).size(), 2U);
}

} // namespace
