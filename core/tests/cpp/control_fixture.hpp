#pragma once

// Three computers in a row over the simulated network, for the input control and drag tests:
// helpers to move, press and command them, and to read what they injected.

#include "control/control.hpp"
#include "group_simulation.hpp"
#include "mesh_simulation.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <set>
#include <variant>
#include <vector>

namespace osdrop::test::control_fixture {

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

inline constexpr std::int64_t kWidth = 1000;
inline constexpr std::int64_t kHeight = 800;
inline constexpr std::uint16_t kKeyA = 0x04;
inline constexpr std::uint16_t kKeyB = 0x05;
inline constexpr std::uint16_t kLeftShift = 0xE1;
inline constexpr std::uint16_t kLeftCommand = 0xE3;
inline constexpr std::uint16_t kLeftControl = 0xE0;
inline constexpr std::uint16_t kCapsLock = 0x39;

inline layout::Screens one_monitor(std::uint8_t key) {
    const layout::Rect frame{.x = 0, .y = 0, .width = kWidth * kUnit, .height = kHeight * kUnit};
    return layout::Screens({layout::Monitor{.key = {key},
                                            .frame = frame,
                                            .native = frame,
                                            .pixel_width = 1,
                                            .pixel_height = 1,
                                            .primary = true}});
}

inline layout::Point at(std::int64_t x, std::int64_t y) {
    return {.x = x * kUnit, .y = y * kUnit};
}

/// What the injected events leave held on a computer.
struct Held {
    std::set<HeldKey> keys;
    std::set<std::uint8_t> buttons;
};

inline Held held_after(const std::vector<control::Injection>& injected) {
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

} // namespace osdrop::test::control_fixture
