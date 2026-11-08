// Cursor and keyboard across computers over the simulated network: crossings in a row of three
// computers, held keys and buttons released on every way out, refusals, local takeover, lost
// peers, cursor reports and the motion latency.

#include "control/control.hpp"
#include "group_simulation.hpp"
#include "mesh_simulation.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace control = osdrop::control;
namespace layout = osdrop::layout;
namespace protocol = osdrop::protocol;
using control::CaptureEnd;
using control::HeldKey;
using layout::kUnit;
using osdrop::test::form_group;
using osdrop::test::SimulatedMeshNetwork;
using osdrop::test::SimulatedNode;
using protocol::ButtonAction;
using protocol::KeyAction;
using std::chrono::milliseconds;

constexpr std::int64_t kWidth = 1000;
constexpr std::int64_t kHeight = 800;
constexpr std::uint16_t kKeyA = 0x04;
constexpr std::uint16_t kKeyB = 0x05;
constexpr std::uint16_t kLeftShift = 0xE1;
constexpr std::uint16_t kLeftCommand = 0xE3;
constexpr std::uint16_t kLeftControl = 0xE0;
constexpr std::uint16_t kCapsLock = 0x39;

layout::Screens one_monitor(std::uint8_t key) {
    const layout::Rect frame{.x = 0, .y = 0, .width = kWidth * kUnit, .height = kHeight * kUnit};
    return layout::Screens({layout::Monitor{.key = {key},
                                            .frame = frame,
                                            .native = frame,
                                            .pixel_width = 1,
                                            .pixel_height = 1,
                                            .primary = true}});
}

layout::Point at(std::int64_t x, std::int64_t y) {
    return {.x = x * kUnit, .y = y * kUnit};
}

/// What the injected events leave held on a computer.
struct Held {
    std::set<HeldKey> keys;
    std::set<std::uint8_t> buttons;
};

Held held_after(const std::vector<control::Injection>& injected) {
    Held held;
    for (const control::Injection& injection : injected) {
        if (const auto* key = std::get_if<protocol::KeyEvent>(&injection)) {
            const HeldKey which{.page = key->page, .usage = key->usage};
            if (key->action == KeyAction::Up) {
                held.keys.erase(which);
            } else {
                held.keys.insert(which);
            }
        } else if (const auto* button = std::get_if<protocol::ButtonEvent>(&injection)) {
            if (button->action == ButtonAction::Up) {
                held.buttons.erase(button->button);
            } else {
                held.buttons.insert(button->button);
            }
        }
    }
    return held;
}

class InputControl : public ::testing::Test {
protected:
    /// Three computers of 1000 x 800 points in a row, 0 | 1 | 2, where the cursor crosses as soon
    /// as it pushes past an edge.
    void SetUp() override {
        network.capture_wire = false;
        network.control_tunables.settings.push.immediate = true;
        form_group(network, 3, platforms);
        layout::Placements row;
        for (std::size_t i = 0; i < 3; ++i) {
            ASSERT_TRUE(
                node(i)
                    .layout->set_screens(one_monitor(static_cast<std::uint8_t>(i + 1)), network.now)
                    .has_value());
        }
        network.run_for(milliseconds(200));
        for (std::size_t i = 0; i < 3; ++i) {
            row[id(i)] = {.offset = {.x = static_cast<std::int64_t>(i) * kWidth * kUnit, .y = 0},
                          .detached = false};
        }
        ASSERT_TRUE(node(0).layout->set_placements(row, network.now).has_value());
        network.run_for(milliseconds(200));
    }

    SimulatedNode& node(std::size_t index) { return network.node(index); }
    layout::DeviceId id(std::size_t index) { return network.node(index).mesh.device_id(); }
    /// The keys injected on a computer, in order.
    std::vector<protocol::KeyEvent> keys_on(std::size_t index) {
        std::vector<protocol::KeyEvent> keys;
        for (const control::Injection& injection : node(index).control_observer.injected) {
            if (const auto* key = std::get_if<protocol::KeyEvent>(&injection)) {
                keys.push_back(*key);
            }
        }
        return keys;
    }
    /// The Caps Lock states set on a computer, in order.
    std::vector<bool> caps_on(std::size_t index) {
        std::vector<bool> states;
        for (const control::Injection& injection : node(index).control_observer.injected) {
            if (const auto* caps = std::get_if<control::SetCapsLock>(&injection)) {
                states.push_back(caps->on);
            }
        }
        return states;
    }
    void settings(std::size_t on, const control::InputSettings& settings) {
        command(on, [&](control::Control& c) { c.set_settings(settings); });
    }

    /// Physical input on computer `on`, then the network runs for `after`.
    void input(std::size_t on, const control::Input& event, milliseconds after = milliseconds(20)) {
        node(on).control->input(event, network.now);
        node(on).mesh.flush(network.now);
        network.run_for(after);
    }
    /// A mouse movement by `dx`, `dy` points with the cursor at `x`, `y` (held there while
    /// captured).
    void move(std::size_t on, std::int64_t x, std::int64_t y, std::int64_t dx, std::int64_t dy,
              milliseconds after = milliseconds(20)) {
        input(on, control::PointerMoved{.position = at(x, y), .delta = at(dx, dy)}, after);
    }
    void key(std::size_t on, std::uint16_t usage, KeyAction action,
             milliseconds after = milliseconds(20)) {
        input(on,
              control::KeyInput{.key = {.page = protocol::kKeyboardPage, .usage = usage},
                                .action = action},
              after);
    }
    void button(std::size_t on, std::uint8_t which, ButtonAction action,
                milliseconds after = milliseconds(20)) {
        input(on, control::ButtonInput{.button = which, .action = action, .click_count = 1}, after);
    }
    /// Computer 0 pushes its cursor out through its right edge, into computer 1.
    void cross_from_0(milliseconds after = milliseconds(20)) {
        move(0, kWidth - 1, 400, 10, 0, after);
    }
    void command(std::size_t on, const std::function<void(control::Control&)>& run,
                 milliseconds after = milliseconds(20)) {
        run(*node(on).control);
        node(on).mesh.flush(network.now);
        network.run_for(after);
    }

    std::vector<protocol::Platform> platforms; ///< a Mac each, unless a fixture says otherwise
    SimulatedMeshNetwork network;
};

/// The same row as a Mac, a PC and a Mac.
class MixedInputControl : public InputControl {
protected:
    MixedInputControl() {
        platforms = {protocol::Platform::MacOS, protocol::Platform::Windows,
                     protocol::Platform::MacOS};
    }
};

TEST_F(InputControl, TheCursorCrossesARowOfComputersAndComesBack) {
    cross_from_0();
    ASSERT_EQ(node(0).control_observer.began.size(), 1U);
    EXPECT_EQ(node(0).control_observer.began.back().device, id(1));
    EXPECT_TRUE(node(0).control->capturing());
    ASSERT_EQ(node(1).control_observer.controlled.size(), 1U);
    EXPECT_EQ(node(1).control_observer.controlled.back(), id(0));
    ASSERT_FALSE(node(1).control_observer.injected.empty());
    EXPECT_EQ(std::get<control::MoveCursor>(node(1).control_observer.injected.front()).position,
              at(0, 400))
        << "it enters at the left edge, at the same height";

    move(0, kWidth - 1, 400, 300, -100);
    EXPECT_EQ(std::get<control::MoveCursor>(node(1).control_observer.injected.back()).position,
              at(300, 300))
        << "motion travels as datagrams";

    key(0, kKeyA, KeyAction::Down);
    key(0, kKeyA, KeyAction::Up);
    const auto& injected = node(1).control_observer.injected;
    ASSERT_GE(injected.size(), 2U);
    EXPECT_EQ(std::get<protocol::KeyEvent>(injected.at(injected.size() - 2)).action,
              KeyAction::Down);
    EXPECT_EQ(std::get<protocol::KeyEvent>(injected.back()).usage, kKeyA);

    move(0, kWidth - 1, 400, 800, 0); // on to computer 2
    EXPECT_EQ(node(1).control_observer.controlled.back(), std::nullopt);
    ASSERT_EQ(node(2).control_observer.controlled.size(), 1U);
    EXPECT_EQ(node(2).control_observer.controlled.back(), id(0));

    move(0, kWidth - 1, 400, -10, 0); // back to computer 1
    EXPECT_EQ(node(2).control_observer.controlled.back(), std::nullopt);
    EXPECT_EQ(node(1).control_observer.controlled.back(), id(0));
    move(0, kWidth - 1, 400, -1100, 0); // home
    EXPECT_EQ(node(1).control_observer.controlled.back(), std::nullopt);
    ASSERT_EQ(node(0).control_observer.ended.size(), 1U);
    const control::CaptureEnded& ended = node(0).control_observer.ended.back();
    EXPECT_EQ(ended.reason, CaptureEnd::Returned);
    EXPECT_EQ(ended.device, id(1));
    EXPECT_GE(ended.cursor.x, (kWidth - 1) * kUnit) << "at the right edge of computer 0";
    EXPECT_FALSE(node(0).control->capturing());
    EXPECT_EQ(node(0).control_observer.began.size(), 1U) << "captured once for the whole trip";
}

TEST_F(InputControl, HeldModifiersCrossAndEverythingIsReleasedOnTheWayOut) {
    key(0, kLeftShift, KeyAction::Down);
    cross_from_0();
    ASSERT_EQ(node(0).control_observer.began.size(), 1U);
    EXPECT_EQ(node(0).control_observer.began.back().release_here,
              (std::vector<HeldKey>{{.page = protocol::kKeyboardPage, .usage = kLeftShift}}))
        << "Shift went down here: it is released here and pressed there";
    EXPECT_EQ(held_after(node(1).control_observer.injected).keys,
              (std::set<HeldKey>{{.page = protocol::kKeyboardPage, .usage = kLeftShift}}));

    key(0, kKeyA, KeyAction::Down);
    button(0, 0, ButtonAction::Down);
    const auto* pressed =
        std::get_if<protocol::ButtonEvent>(&node(1).control_observer.injected.back());
    ASSERT_NE(pressed, nullptr);
    EXPECT_EQ(pressed->x, 0);
    EXPECT_EQ(pressed->y, 400 * kUnit) << "buttons carry the position";
    EXPECT_EQ(held_after(node(1).control_observer.injected).keys.size(), 2U);

    command(0, [](control::Control& c) { c.return_home(protocol::LeaveReason::ReleasedByHotkey); });
    ASSERT_EQ(node(0).control_observer.ended.size(), 1U);
    EXPECT_EQ(node(0).control_observer.ended.back().reason, CaptureEnd::Hotkey);
    EXPECT_EQ(node(0).control_observer.ended.back().cursor, at(kWidth - 1, 400))
        << "the cursor reappears where it left";
    const Held left = held_after(node(1).control_observer.injected);
    EXPECT_TRUE(left.keys.empty());
    EXPECT_TRUE(left.buttons.empty());
    EXPECT_EQ(node(1).control_observer.controlled.back(), std::nullopt);
}

TEST_F(InputControl, ReleaseAllReleasesWithoutEndingTheControl) {
    cross_from_0();
    key(0, kKeyA, KeyAction::Down);
    // The first crossing of computer 0 has control_id 1.
    ASSERT_TRUE(node(0)
                    .mesh.send_message(id(1), protocol::ReleaseAll{.control_id = 1}, network.now)
                    .has_value());
    network.run_for(milliseconds(20));
    EXPECT_TRUE(held_after(node(1).control_observer.injected).keys.empty());
    EXPECT_EQ(node(1).control_observer.controlled.back(), id(0));
    key(0, kKeyB, KeyAction::Down);
    EXPECT_EQ(std::get<protocol::KeyEvent>(node(1).control_observer.injected.back()).usage, kKeyB);
}

TEST_F(InputControl, AHeldButtonOrTheLockKeepsTheCursorWhereItIs) {
    button(0, 0, ButtonAction::Down);
    cross_from_0();
    EXPECT_TRUE(node(0).control_observer.began.empty()) << "no crossing with a button held";
    button(0, 0, ButtonAction::Up);
    cross_from_0();
    EXPECT_EQ(node(0).control_observer.began.size(), 1U);

    command(0, [](control::Control& c) { c.set_locked(true); });
    move(0, kWidth - 1, 400, 2000, 0);
    EXPECT_TRUE(node(2).control_observer.controlled.empty()) << "locked on computer 1";
    EXPECT_EQ(node(1).control_observer.controlled.back(), id(0));
    command(0, [](control::Control& c) { c.set_locked(false); });
    move(0, kWidth - 1, 400, 10, 0);
    EXPECT_EQ(node(2).control_observer.controlled.back(), id(0));
}

TEST_F(InputControl, LocalInputOnTheControlledComputerTakesItBack) {
    cross_from_0();
    key(0, kKeyA, KeyAction::Down);
    button(0, 1, ButtonAction::Down);
    move(1, 500, 500, 1, 0); // someone moves computer 1's own mouse
    ASSERT_EQ(node(0).control_observer.ended.size(), 1U);
    EXPECT_EQ(node(0).control_observer.ended.back().reason, CaptureEnd::Takeover);
    EXPECT_EQ(node(0).control_observer.ended.back().cursor, at(kWidth - 1, 400));
    const Held left = held_after(node(1).control_observer.injected);
    EXPECT_TRUE(left.keys.empty());
    EXPECT_TRUE(left.buttons.empty());
    EXPECT_EQ(node(1).control_observer.controlled.back(), std::nullopt);

    key(0, kKeyB, KeyAction::Down);
    EXPECT_EQ(held_after(node(1).control_observer.injected).keys.size(), 0U)
        << "input after the takeover stays home";
}

TEST_F(InputControl, ALostEndpointSendsTheCursorHomeWithinASecond) {
    cross_from_0();
    node(1).frozen = true;
    network.run_for(milliseconds(1'000));
    ASSERT_EQ(node(0).control_observer.ended.size(), 1U);
    EXPECT_EQ(node(0).control_observer.ended.back().reason, CaptureEnd::PeerLost);
    EXPECT_EQ(node(0).control_observer.ended.back().cursor, at(kWidth - 1, 400));
    EXPECT_FALSE(node(0).control->capturing());
}

TEST_F(InputControl, ALostControllerLeavesNothingPressed) {
    cross_from_0();
    key(0, kKeyA, KeyAction::Down);
    button(0, 0, ButtonAction::Down);
    node(0).frozen = true;
    network.run_for(milliseconds(1'000));
    const Held left = held_after(node(1).control_observer.injected);
    EXPECT_TRUE(left.keys.empty());
    EXPECT_TRUE(left.buttons.empty());
    EXPECT_EQ(node(1).control_observer.controlled.back(), std::nullopt);
}

TEST_F(InputControl, RefusalsSendTheCursorHome) {
    command(1, [](control::Control& c) { c.set_unavailable(protocol::RefuseReason::Locked); });
    cross_from_0();
    ASSERT_EQ(node(0).control_observer.ended.size(), 1U);
    EXPECT_EQ(node(0).control_observer.ended.back().reason, CaptureEnd::Refused);
    EXPECT_EQ(node(0).control_observer.ended.back().refused, protocol::RefuseReason::Locked);
    EXPECT_EQ(node(0).control_observer.ended.back().cursor, at(kWidth - 1, 400));
    EXPECT_TRUE(node(1).control_observer.controlled.empty());
    command(1, [](control::Control& c) { c.set_unavailable(std::nullopt); });

    // Computer 1 is busy controlling computer 2.
    move(1, kWidth - 1, 400, 10, 0);
    EXPECT_EQ(node(2).control_observer.controlled.back(), id(1));
    cross_from_0();
    ASSERT_EQ(node(0).control_observer.ended.size(), 2U);
    EXPECT_EQ(node(0).control_observer.ended.back().refused, protocol::RefuseReason::Busy);
}

TEST_F(InputControl, ScreenLockDuringControlReleasesIt) {
    cross_from_0();
    key(0, kKeyA, KeyAction::Down);
    command(1, [](control::Control& c) { c.set_unavailable(protocol::RefuseReason::Locked); });
    EXPECT_TRUE(held_after(node(1).control_observer.injected).keys.empty());
    ASSERT_EQ(node(0).control_observer.ended.size(), 1U);
    EXPECT_EQ(node(0).control_observer.ended.back().refused, protocol::RefuseReason::Locked);
}

TEST_F(InputControl, AComputerThatDeniesControlIsAWall) {
    osdrop::group::Permissions denied;
    denied.control = false;
    ASSERT_TRUE(node(1).group->set_permissions(id(0), denied, network.now).has_value());
    network.run_for(milliseconds(200));
    cross_from_0();
    EXPECT_TRUE(node(0).control_observer.began.empty());
    EXPECT_TRUE(node(1).control_observer.controlled.empty());
}

TEST_F(InputControl, DenyingControlDuringControlEndsIt) {
    cross_from_0();
    key(0, kKeyA, KeyAction::Down);
    osdrop::group::Permissions denied;
    denied.control = false;
    ASSERT_TRUE(node(1).group->set_permissions(id(0), denied, network.now).has_value());
    network.run_for(milliseconds(200));
    EXPECT_TRUE(held_after(node(1).control_observer.injected).keys.empty());
    EXPECT_EQ(node(1).control_observer.controlled.back(), std::nullopt);
    ASSERT_FALSE(node(0).control_observer.ended.empty());
    EXPECT_FALSE(node(0).control->capturing());
}

TEST_F(InputControl, CursorReportsMoveTheVirtualCursor) {
    cross_from_0();
    command(1, [](control::Control& c) { c.cursor_moved(at(500, 100)); });
    button(0, 0, ButtonAction::Down);
    const auto* pressed =
        std::get_if<protocol::ButtonEvent>(&node(1).control_observer.injected.back());
    ASSERT_NE(pressed, nullptr);
    EXPECT_EQ(pressed->x, 500 * kUnit);
    EXPECT_EQ(pressed->y, 100 * kUnit);
}

TEST_F(InputControl, MotionLatencyIsMeasuredThroughTheClockOffset) {
    cross_from_0();
    network.run_for(milliseconds(2'500)); // pings measure the clock offset
    for (int i = 0; i < 50; ++i) {
        move(0, kWidth - 1, 400, 1, 0, milliseconds(10));
    }
    network.run_for(milliseconds(1'500));
    ASSERT_FALSE(node(1).control_observer.latencies.empty());
    const auto& [controller, stats] = node(1).control_observer.latencies.back();
    EXPECT_EQ(controller, id(0));
    EXPECT_GE(stats.samples, 50U);
    EXPECT_LE(stats.median, milliseconds(2))
        << "the simulated network delays by half a millisecond";
    EXPECT_LE(stats.p99, milliseconds(2));
}

/// DEVPLAN Phase 11: 1000 crossings with keys and buttons held leave nothing pressed anywhere.
TEST_F(InputControl, AThousandCrossingsLeaveNoKeyOrButtonPressed) {
    std::mt19937 random(11); // NOLINT(cert-msc32-c,cert-msc51-cpp): a fixed, repeatable run
    const auto chance = [&](int percent) {
        return std::uniform_int_distribution<int>(0, 99)(random) < percent;
    };
    const std::vector<std::uint16_t> keys{kKeyA, kKeyB, kLeftShift, kLeftCommand};
    const milliseconds step(2);
    std::set<std::uint16_t> down;
    const auto press_some = [&] {
        for (const std::uint16_t usage : keys) {
            if (chance(30)) {
                const bool held = down.contains(usage);
                key(0, usage, held ? KeyAction::Up : KeyAction::Down, step);
                if (held) {
                    down.erase(usage);
                } else {
                    down.insert(usage);
                }
            }
        }
    };
    for (int crossing = 0; crossing < 1'000; ++crossing) {
        press_some();
        cross_from_0(step);
        ASSERT_TRUE(node(0).control->capturing()) << crossing;
        press_some();
        const bool holds_button = chance(50);
        if (holds_button) {
            button(0, static_cast<std::uint8_t>(crossing % 3), ButtonAction::Down, step);
        }
        if (!holds_button && chance(30)) {
            move(0, kWidth - 1, 400, 1100, 0, step); // on to computer 2
        }
        press_some();
        switch (crossing % 3) {
        case 0:
            command(
                0,
                [](control::Control& c) { c.return_home(protocol::LeaveReason::ReleasedByHotkey); },
                step);
            break;
        case 1: {
            const std::size_t endpoint =
                node(1).control->controller() ? std::size_t{1} : std::size_t{2};
            move(endpoint, 500, 500, 1, 0, step); // local takeover
            break;
        }
        default:
            if (holds_button) {
                button(0, static_cast<std::uint8_t>(crossing % 3), ButtonAction::Up, step);
            }
            move(0, kWidth - 1, 400, -2200, 0, step);
            move(0, kWidth - 1, 400, -2200, 0, step);
            break;
        }
        if (holds_button && crossing % 3 != 2) {
            button(0, static_cast<std::uint8_t>(crossing % 3), ButtonAction::Up, step);
        }
        ASSERT_FALSE(node(0).control->capturing()) << crossing;
    }
    for (const std::size_t endpoint : {std::size_t{1}, std::size_t{2}}) {
        const Held left = held_after(node(endpoint).control_observer.injected);
        EXPECT_TRUE(left.keys.empty()) << endpoint;
        EXPECT_TRUE(left.buttons.empty()) << endpoint;
        EXPECT_EQ(node(endpoint).control_observer.controlled.back(), std::nullopt);
    }
    EXPECT_EQ(node(0).control_observer.began.size(), 1'000U);
}

TEST_F(MixedInputControl, ShortcutsKeepTheirPlaceBetweenAMacAndAPc) {
    const HeldKey command{.page = protocol::kKeyboardPage, .usage = kLeftCommand};
    const HeldKey control{.page = protocol::kKeyboardPage, .usage = kLeftControl};
    key(0, kLeftCommand, KeyAction::Down);
    cross_from_0(); // the Mac's ⌘ goes to the PC as Control
    EXPECT_EQ(held_after(node(1).control_observer.injected).keys, std::set<HeldKey>{control});
    key(0, kKeyA, KeyAction::Down);
    key(0, kKeyA, KeyAction::Up);
    key(0, kLeftControl, KeyAction::Down); // and the Mac's Control as the Windows key
    EXPECT_EQ(keys_on(1).back().usage, kLeftCommand);
    key(0, kLeftControl, KeyAction::Up);
    key(0, kLeftCommand, KeyAction::Up);
    EXPECT_TRUE(held_after(node(1).control_observer.injected).keys.empty());

    // Mac to Mac: keys as they are.
    key(0, kLeftCommand, KeyAction::Down);
    move(0, kWidth - 1, 400, 1100, 0);
    ASSERT_EQ(node(2).control_observer.controlled.back(), id(0));
    EXPECT_EQ(held_after(node(2).control_observer.injected).keys, std::set<HeldKey>{command});
    key(0, kLeftCommand, KeyAction::Up);
}

TEST_F(MixedInputControl, KeysByPositionWhenChosen) {
    control::InputSettings by_position;
    by_position.modifier_mapping = control::ModifierMapping::Positions;
    by_position.push.immediate = true;
    settings(0, by_position);
    key(0, kLeftCommand, KeyAction::Down);
    cross_from_0();
    EXPECT_EQ(held_after(node(1).control_observer.injected).keys,
              (std::set<HeldKey>{{.page = protocol::kKeyboardPage, .usage = kLeftCommand}}));
}

TEST_F(MixedInputControl, AMappingChangedDuringControlWaitsForTheNextCrossing) {
    key(0, kLeftCommand, KeyAction::Down);
    cross_from_0();
    control::InputSettings by_position;
    by_position.modifier_mapping = control::ModifierMapping::Positions;
    by_position.push.immediate = true;
    settings(0, by_position);
    key(0, kLeftCommand, KeyAction::Up);
    EXPECT_TRUE(held_after(node(1).control_observer.injected).keys.empty())
        << "the release matches its press";
}

TEST_F(InputControl, CapsLockFollowsTheUser) {
    command(0, [](control::Control& c) { c.set_caps_lock(true); });
    cross_from_0();
    EXPECT_EQ(caps_on(1), std::vector<bool>{true}) << "on over there, as here";
    key(0, kCapsLock, KeyAction::Down);
    key(0, kCapsLock, KeyAction::Up);
    EXPECT_EQ(caps_on(1), (std::vector<bool>{true, false})) << "a press toggles it there";
    EXPECT_TRUE(keys_on(1).empty()) << "Caps Lock travels as a state, never as a held key";
    command(0, [](control::Control& c) { c.set_caps_lock(true); }); // stale: the cursor is away
    command(0, [](control::Control& c) { c.return_home(protocol::LeaveReason::ReleasedByHotkey); });
    ASSERT_EQ(node(0).control_observer.ended.size(), 1U);
    EXPECT_FALSE(node(0).control_observer.ended.back().caps_lock)
        << "back home as the user left it";
}

TEST_F(InputControl, CrossingOnlyWithAModifierWhenChosen) {
    control::InputSettings with_control;
    with_control.crossing_modifiers = 0b0001'0001; // either Control key
    with_control.push.immediate = true;
    settings(0, with_control);
    cross_from_0();
    EXPECT_TRUE(node(0).control_observer.began.empty());
    key(0, kLeftControl, KeyAction::Down);
    cross_from_0();
    EXPECT_EQ(node(0).control_observer.began.size(), 1U);
}

TEST_F(InputControl, APushMustLastAsLongAsChosen) {
    control::InputSettings slow;
    slow.push.time = milliseconds(300);
    settings(0, slow);
    for (int i = 0; i < 10; ++i) { // 200 ms of pushing
        move(0, kWidth - 1, 400, 10, 0);
    }
    EXPECT_TRUE(node(0).control_observer.began.empty());
    for (int i = 0; i < 10; ++i) { // past 300 ms
        move(0, kWidth - 1, 400, 10, 0);
    }
    EXPECT_EQ(node(0).control_observer.began.size(), 1U);
}

TEST_F(InputControl, PointerSpeedPerComputer) {
    command(0, [&](control::Control& c) { c.set_speed(id(1), 2'000); });
    cross_from_0();
    move(0, kWidth - 1, 400, 100, 0);
    EXPECT_EQ(std::get<control::MoveCursor>(node(1).control_observer.injected.back()).position,
              at(200, 400))
        << "twice as fast on computer 1";
}

} // namespace
