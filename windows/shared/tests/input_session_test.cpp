// OsdropInput's handling of one client (DEVPLAN Phases 3 and 11): injection only while the
// client controls this PC or captures its input, invalid messages refused and logged, a capture
// that ends here when its heartbeat stops or the return chord is pressed, and nothing left pressed
// or captured when control ends or the client goes.
#include <osdrop/win/input_session.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace {

namespace input = osdrop::win::input;
using osdrop::win::ScanCode;

const ScanCode kShift{.code = 0x2A, .extended = false};
const ScanCode kA{.code = 0x1E, .extended = false};

struct FakeDevice final : input::InputDevice {
    std::vector<std::string> calls;

    void move(std::int32_t x, std::int32_t y) override {
        calls.push_back("move " + std::to_string(x) + "," + std::to_string(y));
    }
    void press(input::Button button, bool down) override {
        calls.push_back("press " + std::to_string(static_cast<int>(button)) +
                        (down ? " down" : " up"));
    }
    void scroll(std::int32_t vertical, std::int32_t horizontal) override {
        calls.push_back("scroll " + std::to_string(vertical) + "," + std::to_string(horizontal));
    }
    void key(ScanCode scan, bool down) override {
        calls.push_back("key " + std::to_string(scan.code) + (down ? " down" : " up"));
    }
    bool press_held_left() override {
        if (left_held) {
            calls.push_back("drag press");
        }
        return left_held;
    }
    bool left_held = false; ///< this PC's own left button
    void start_capture(std::int32_t x, std::int32_t y) override {
        calls.push_back("capture " + std::to_string(x) + "," + std::to_string(y));
    }
    void stop_capture(std::int32_t x, std::int32_t y) override {
        calls.push_back("release " + std::to_string(x) + "," + std::to_string(y));
    }
    void set_controlled(bool on) override { controlled = on; }
    void set_watching(bool on) override { calls.push_back(on ? "watch" : "unwatch"); }
    void mask_menu_keys() override { calls.emplace_back("mask"); }
    void set_chords(const input::Chord& home, const input::Chord& lock) override {
        calls.push_back("chords " + std::to_string(home.key.code) + "," +
                        std::to_string(lock.key.code));
    }
    bool controlled = false;
    [[nodiscard]] input::Stats stats() const override {
        return {.events = 5, .p50_us = 1, .p99_us = 3, .max_us = 4};
    }
};

struct FakeLog final : input::InputLog {
    std::vector<std::string> warnings;
    void info(std::string_view /*message*/) override {}
    void warning(std::string_view message) override { warnings.emplace_back(message); }
};

using Clock = input::InputSession::Clock;

class InputSessionTest : public ::testing::Test {
protected:
    std::optional<input::Report> send(const input::Request& request) {
        return session.handle(input::encode(request), now);
    }

    Clock::time_point now = Clock::time_point(std::chrono::hours(1));
    FakeDevice device;
    FakeLog log;
    input::InputSession session{device, log};
};

TEST_F(InputSessionTest, InjectionWithoutControlIsRefusedAndLogged) {
    const auto reply = send(input::Move{.x = 1, .y = 2});
    ASSERT_TRUE(reply.has_value());
    const auto* rejected = std::get_if<input::Rejected>(&*reply);
    ASSERT_NE(rejected, nullptr);
    EXPECT_EQ(rejected->reason, input::Refusal::NotControlling);
    EXPECT_EQ(rejected->type, input::type_of(input::Request(input::Move{})));
    EXPECT_TRUE(device.calls.empty());
    ASSERT_EQ(log.warnings.size(), 1U);
    EXPECT_NE(log.warnings.front().find("Move"), std::string::npos);
}

TEST_F(InputSessionTest, InjectionDuringControlReachesTheDevice) {
    EXPECT_FALSE(send(input::BeginControl{}).has_value());
    EXPECT_TRUE(device.controlled);
    EXPECT_FALSE(send(input::Move{.x = -5, .y = 7}).has_value());
    EXPECT_FALSE(send(input::Press{.button = input::Button::Right, .down = true}).has_value());
    EXPECT_FALSE(send(input::Scroll{.vertical = 120, .horizontal = 0}).has_value());
    EXPECT_FALSE(send(input::Key{.scan = kA, .down = true}).has_value());
    EXPECT_EQ(device.calls, (std::vector<std::string>{"move -5,7", "press 1 down", "scroll 120,0",
                                                      "key 30 down"}));
    EXPECT_TRUE(log.warnings.empty());
}

TEST_F(InputSessionTest, EndingControlReleasesWhatIsStillDown) {
    send(input::BeginControl{});
    send(input::Key{.scan = kShift, .down = true});
    send(input::Key{.scan = kA, .down = true});
    send(input::Key{.scan = kA, .down = false});
    send(input::Press{.button = input::Button::Left, .down = true});
    device.calls.clear();
    send(input::EndControl{});
    EXPECT_EQ(device.calls, (std::vector<std::string>{"key 42 up", "press 0 up"}));
    EXPECT_FALSE(session.controlling());
    EXPECT_FALSE(device.controlled);
    ASSERT_TRUE(send(input::Key{.scan = kA, .down = true}).has_value()) << "refused again";
}

TEST_F(InputSessionTest, AHeldDragKeepsTheLeftButtonDownPastTheControl) {
    send(input::BeginControl{});
    send(input::Key{.scan = kShift, .down = true});
    send(input::Press{.button = input::Button::Left, .down = true});
    EXPECT_FALSE(send(input::HoldDrag{}).has_value());
    device.calls.clear();
    send(input::EndControl{});
    EXPECT_EQ(device.calls, std::vector<std::string>{"key 42 up"}) << "the drag waits";
    // Injection is refused now, but the drag's own release is not.
    EXPECT_FALSE(send(input::ReleaseDrag{}).has_value());
    EXPECT_EQ(device.calls, (std::vector<std::string>{"key 42 up", "press 0 up"}));
    EXPECT_TRUE(log.warnings.empty());
}

TEST_F(InputSessionTest, ADragIsReleasedWhileCapturingToo) {
    send(input::StartCapture{.park_x = 100, .park_y = 200, .home_x = 0, .home_y = 300});
    device.calls.clear();
    EXPECT_FALSE(send(input::ReleaseDrag{}).has_value());
    EXPECT_EQ(device.calls, std::vector<std::string>{"press 0 up"});
}

TEST_F(InputSessionTest, AClientThatGoesReleasesAHeldDrag) {
    send(input::BeginControl{});
    send(input::Press{.button = input::Button::Left, .down = true});
    send(input::HoldDrag{});
    send(input::EndControl{});
    device.calls.clear();
    session.reset();
    EXPECT_EQ(device.calls, std::vector<std::string>{"press 0 up"});
}

TEST_F(InputSessionTest, ADragThatCameHomeIsPressedOnlyWhileThisPcsLeftButtonIsHeld) {
    const auto refused = send(input::PressForDrag{});
    ASSERT_TRUE(refused.has_value()) << "the hand let go: nothing would release it";
    EXPECT_EQ(std::get<input::Rejected>(*refused).reason, input::Refusal::NotHeld);
    EXPECT_TRUE(device.calls.empty());
    EXPECT_EQ(log.warnings.size(), 1U);

    device.left_held = true;
    EXPECT_FALSE(send(input::PressForDrag{}).has_value())
        << "neither control nor capture is needed";
    EXPECT_EQ(device.calls, std::vector<std::string>{"drag press"});
}

TEST_F(InputSessionTest, TheHandsReleaseEndsADragThatCameHome) {
    device.left_held = true;
    send(input::PressForDrag{});
    session.forwarding(input::PhysicalPress{.button = input::Button::Left, .down = false});
    device.calls.clear();
    session.reset();
    EXPECT_TRUE(device.calls.empty()) << "already released by the hand";
}

TEST_F(InputSessionTest, AClientThatGoesReleasesADragThatCameHome) {
    device.left_held = true;
    send(input::PressForDrag{});
    session.forwarding(input::PhysicalPress{.button = input::Button::Right, .down = false});
    device.calls.clear();
    session.reset();
    EXPECT_EQ(device.calls, std::vector<std::string>{"press 0 up"});
}

TEST_F(InputSessionTest, AClientThatGoesLeavesNothingPressedOrCaptured) {
    send(input::Watch{.on = true});
    send(input::BeginControl{});
    send(input::Press{.button = input::Button::Middle, .down = true});
    send(input::StartCapture{.park_x = 100, .park_y = 200, .home_x = 1919, .home_y = 300});
    EXPECT_TRUE(session.capturing());
    device.calls.clear();
    session.reset();
    EXPECT_EQ(device.calls, (std::vector<std::string>{"release 1919,300", "press 2 up", "unwatch"}))
        << "the cursor shows where it left";
    EXPECT_FALSE(session.capturing());
    EXPECT_FALSE(session.controlling());
    EXPECT_FALSE(session.watching());
    EXPECT_FALSE(device.controlled);
}

TEST_F(InputSessionTest, CaptureStartsAndStopsWithoutControl) {
    send(input::StartCapture{.park_x = 1, .park_y = 2});
    send(input::StopCapture{.x = 30, .y = 40});
    send(input::StopCapture{.x = 50, .y = 60}); // not capturing any more: nothing to do
    EXPECT_EQ(device.calls, (std::vector<std::string>{"capture 1,2", "release 30,40"}));
}

TEST_F(InputSessionTest, CapturingMayReleaseKeysHereAndSetCapsLock) {
    send(input::StartCapture{.park_x = 1, .park_y = 2});
    EXPECT_FALSE(send(input::Key{.scan = kShift, .down = false}).has_value());
    EXPECT_FALSE(send(input::Key{.scan = kA, .down = true}).has_value());
    send(input::StopCapture{.x = 3, .y = 4});
    EXPECT_EQ(device.calls, (std::vector<std::string>{"capture 1,2", "key 42 up", "key 30 down",
                                                      "release 3,4", "key 30 up"}))
        << "what the capture pressed is released with it";
    ASSERT_TRUE(send(input::Key{.scan = kA, .down = false}).has_value())
        << "refused once the capture is over";
}

TEST_F(InputSessionTest, ACaptureWithoutHeartbeatEndsWhereTheCursorLeft) {
    send(input::StartCapture{.park_x = 960, .park_y = 540, .home_x = 1919, .home_y = 200});
    ASSERT_EQ(session.deadline(), now + input::kHeartbeatTimeout);
    now += input::kHeartbeatInterval;
    send(input::Heartbeat{});
    EXPECT_FALSE(session.expire(now + input::kHeartbeatTimeout - std::chrono::milliseconds(1)));
    device.calls.clear();
    const auto released = session.expire(now + input::kHeartbeatTimeout);
    ASSERT_TRUE(released.has_value());
    EXPECT_EQ(std::get<input::Released>(*released).reason, input::ReleaseReason::Watchdog);
    EXPECT_EQ(device.calls, (std::vector<std::string>{"release 1919,200"}));
    EXPECT_FALSE(session.capturing());
    EXPECT_FALSE(session.deadline().has_value());
    EXPECT_FALSE(session.expire(now + std::chrono::hours(1)).has_value()) << "once";
    EXPECT_EQ(log.warnings.size(), 1U);

    send(input::Heartbeat{}); // late: no error
    send(input::StopCapture{.x = 1919, .y = 210});
    EXPECT_EQ(device.calls, (std::vector<std::string>{"release 1919,200", "release 1919,210"}))
        << "the client's StopCapture still places the cursor";
}

TEST_F(InputSessionTest, TheReturnChordEndsTheCaptureHere) {
    send(input::StartCapture{.park_x = 960, .park_y = 540, .home_x = 0, .home_y = 700});
    device.calls.clear();
    session.forwarding(input::Released{.reason = input::ReleaseReason::ReturnChord});
    EXPECT_EQ(device.calls, (std::vector<std::string>{"release 0,700"}));
    EXPECT_FALSE(session.capturing());
    session.forwarding(input::Released{.reason = input::ReleaseReason::ReturnChord});
    EXPECT_EQ(device.calls.size(), 1U) << "a second one has nothing to end";
    EXPECT_FALSE(session.expire(now + std::chrono::hours(1)).has_value());
}

TEST_F(InputSessionTest, HotkeysOutsideCaptureMaskTheMenuKeys) {
    session.forwarding(input::HotkeyPressed{.hotkey = input::Hotkey::LockInput});
    send(input::StartCapture{.park_x = 1, .park_y = 2});
    session.forwarding(input::HotkeyPressed{.hotkey = input::Hotkey::LockInput});
    session.forwarding(input::PhysicalKey{.scan = kA, .down = true});
    EXPECT_EQ(device.calls, (std::vector<std::string>{"mask", "capture 1,2"}))
        << "during capture no modifier reached an app";
}

TEST_F(InputSessionTest, WatchingFollowsTheClient) {
    send(input::Watch{.on = true});
    send(input::Watch{.on = true});
    EXPECT_TRUE(session.watching());
    send(input::Watch{.on = false});
    EXPECT_EQ(device.calls, (std::vector<std::string>{"watch", "unwatch"}));
}

TEST_F(InputSessionTest, TheClientsChordsLastUntilItGoes) {
    const input::SetChords chords{.return_home = {.key = kA, .modifiers = input::kControl},
                                  .lock_input = input::kLockInputChord};
    EXPECT_FALSE(send(chords).has_value());
    send(chords);
    session.reset();
    session.reset();
    EXPECT_EQ(device.calls, (std::vector<std::string>{"chords 30,37", "chords 1,37"}))
        << "set once, and the defaults again for the next client";
}

TEST_F(InputSessionTest, StatsAreAnswered) {
    const auto reply = send(input::QueryStats{});
    ASSERT_TRUE(reply.has_value());
    EXPECT_EQ(std::get<input::Stats>(*reply).p99_us, 3U);
}

TEST_F(InputSessionTest, BrokenMessagesAreRefusedAndLogged) {
    auto newer = input::encode(input::Request(input::BeginControl{}));
    newer[4] = input::kVersion + 1;
    const auto version = session.handle(newer, now);
    ASSERT_TRUE(version.has_value());
    EXPECT_EQ(std::get<input::Rejected>(*version).reason, input::Refusal::UnsupportedVersion);
    EXPECT_FALSE(session.controlling());

    const std::vector<std::uint8_t> garbage{1, 2, 3};
    const auto malformed = session.handle(garbage, now);
    ASSERT_TRUE(malformed.has_value());
    EXPECT_EQ(std::get<input::Rejected>(*malformed).reason, input::Refusal::Malformed);
    EXPECT_EQ(std::get<input::Rejected>(*malformed).type, 0);
    EXPECT_EQ(log.warnings.size(), 2U);
    EXPECT_TRUE(device.calls.empty());
}

} // namespace
