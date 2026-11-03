// Crossing between computers: neighbours across the shared arrangement (property tests over random
// arrangements of macOS and mixed-scale Windows desktops), edge push timing with a fake clock,
// the virtual cursor on another computer, pointer speed, and the way home.
#include "layout/arrangement.hpp"
#include "layout/crossing.hpp"
#include "layout/document.hpp"
#include "layout/edge_push.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <set>
#include <vector>

namespace {

using namespace std::chrono_literals;
using osdrop::TimePoint;
using osdrop::layout::Arrangement;
using osdrop::layout::CrossingEngine;
using osdrop::layout::CursorUpdate;
using osdrop::layout::DeviceId;
using osdrop::layout::Edge;
using osdrop::layout::EdgeContact;
using osdrop::layout::EdgePush;
using osdrop::layout::kSnapTolerance;
using osdrop::layout::kUnit;
using osdrop::layout::Monitor;
using osdrop::layout::Placement;
using osdrop::layout::Placements;
using osdrop::layout::Point;
using osdrop::layout::PushConfig;
using osdrop::layout::Rect;
using osdrop::layout::ScreenMap;
using osdrop::layout::Screens;

const DeviceId kMac{1};
const DeviceId kPc{2};
const DeviceId kLaptop{3};

Rect units(std::int64_t x, std::int64_t y, std::int64_t width, std::int64_t height) {
    return {.x = x * kUnit, .y = y * kUnit, .width = width * kUnit, .height = height * kUnit};
}

Point at(std::int64_t x, std::int64_t y) {
    return {.x = x * kUnit, .y = y * kUnit};
}

Monitor mac(std::uint8_t key, Rect frame, bool primary = false) {
    return {.key = {key},
            .frame = frame,
            .native = frame,
            .pixel_width = 1,
            .pixel_height = 1,
            .primary = primary};
}

/// Windows monitors from physical rectangles and scales in percent, with derived frames.
Screens windows(const std::vector<std::pair<Rect, std::int64_t>>& monitors) {
    std::vector<Monitor> list;
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        const auto& [native, percent] = monitors[i];
        list.push_back({.key = {static_cast<std::uint8_t>(i + 1)},
                        .frame = {.x = 0,
                                  .y = 0,
                                  .width = native.width * 100 / percent,
                                  .height = native.height * 100 / percent},
                        .native = native,
                        .pixel_width = 1,
                        .pixel_height = 1,
                        .primary = i == 0});
    }
    return Screens(osdrop::layout::derive_frames(std::move(list)));
}

Edge opposite(Edge edge) {
    switch (edge) {
    case Edge::Left:
        return Edge::Right;
    case Edge::Right:
        return Edge::Left;
    case Edge::Top:
        return Edge::Bottom;
    case Edge::Bottom:
        return Edge::Top;
    }
    return edge;
}

/// Native points on one edge of a monitor, every `step` units.
std::vector<Point> along(const Rect& native, Edge edge, std::int64_t step) {
    std::vector<Point> points;
    const bool vertical = edge == Edge::Left || edge == Edge::Right;
    const std::int64_t length = vertical ? native.height : native.width;
    for (std::int64_t t = 0; t < length; t += step) {
        switch (edge) {
        case Edge::Left:
            points.push_back({.x = native.left(), .y = native.top() + t});
            break;
        case Edge::Right:
            points.push_back({.x = native.right() - 1, .y = native.top() + t});
            break;
        case Edge::Top:
            points.push_back({.x = native.left() + t, .y = native.top()});
            break;
        case Edge::Bottom:
            points.push_back({.x = native.left() + t, .y = native.bottom() - 1});
            break;
        }
    }
    return points;
}

/// A random desktop: macOS (one or two monitors, logical equals native) or Windows (one or two
/// monitors at random scales, frames derived from the physical layout).
Screens random_desktop(std::mt19937& random) {
    const auto size = [&](std::int64_t low, std::int64_t span) {
        return low + static_cast<std::int64_t>(random() % static_cast<std::uint32_t>(span));
    };
    const bool two = random() % 2 == 0;
    if (random() % 2 == 0) {
        std::vector<Monitor> monitors{mac(1, units(0, 0, size(1000, 2000), size(700, 1000)), true)};
        if (two) {
            const Rect first = monitors[0].frame;
            const std::int64_t height = size(500, 1200);
            const std::int64_t shift = size(-height + 50, height + (first.height / kUnit) - 100);
            monitors.push_back(
                random() % 2 == 0
                    ? mac(2, units(first.width / kUnit, shift, size(800, 2000), height))
                    : mac(2, units(size(-300, 600), -height, size(800, 2000), height)));
        }
        return Screens(monitors);
    }
    constexpr std::array<std::int64_t, 5> kScales{100, 125, 150, 175, 200};
    const auto scale = [&] { return kScales.at(random() % kScales.size()); };
    std::vector<std::pair<Rect, std::int64_t>> monitors{
        {units(0, 0, size(1280, 2560), size(800, 1400)), scale()}};
    if (two) {
        const Rect first = monitors[0].first;
        const std::int64_t height = size(700, 1500);
        const std::int64_t shift = size(-height + 50, height + (first.height / kUnit) - 100);
        monitors.push_back({units(first.width / kUnit, shift, size(1280, 2560), height), scale()});
    }
    return windows(monitors);
}

/// Computers placed one after another to the right, each touching the previous ones somewhere
/// along its height, or below them.
struct RandomArrangement {
    ScreenMap screens;
    Placements placements;
    Arrangement arrangement;
};

RandomArrangement random_arrangement(std::mt19937& random) {
    RandomArrangement result;
    const std::size_t count = 2 + (random() % 3);
    std::set<DeviceId> all;
    for (std::size_t i = 0; i < count; ++i) {
        const DeviceId device{static_cast<std::uint8_t>(i + 1)};
        const Screens desktop = random_desktop(random);
        Point offset = osdrop::layout::place_beside(result.placements, result.screens, desktop);
        if (i > 0 && random() % 3 == 0) { // below everything, left-aligned
            std::int64_t bottom = 0;
            for (const auto& [other, placement] : result.placements) {
                for (const Rect& frame :
                     osdrop::layout::global_frames(result.screens.at(other), placement)) {
                    bottom = std::max(bottom, frame.bottom());
                }
            }
            const Rect bounds = desktop.logical_bounds();
            offset = {.x = -bounds.x, .y = bottom - bounds.y};
        }
        result.screens[device] = desktop;
        result.placements[device] = {.offset = offset, .detached = false};
        all.insert(device);
    }
    result.arrangement = Arrangement::build(result.placements, result.screens, all);
    return result;
}

TEST(LayoutCrossing, LeavingAndComingBackThroughTheSameEdgeReturnsWithinOneUnit) {
    std::size_t crossings = 0;
    for (std::uint32_t seed = 1; seed <= 300; ++seed) {
        SCOPED_TRACE(seed);
        std::mt19937 random(seed);
        const RandomArrangement setup = random_arrangement(random);
        for (const auto& [device, screens] : setup.screens) {
            for (std::size_t m = 0; m < screens.monitors().size(); ++m) {
                for (const Edge edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
                    for (const Point exit :
                         along(screens.monitors()[m].native, edge, 7 * kUnit + 3)) {
                        if (!screens.at_outer_edge(m, edge, exit)) {
                            continue;
                        }
                        const auto entry = setup.arrangement.neighbour(device, m, edge, exit);
                        if (!entry) {
                            continue;
                        }
                        const Screens& target = setup.screens.at(entry->device);
                        ASSERT_TRUE(
                            target.monitors()[entry->monitor].native.contains(entry->position));
                        if (!target.at_outer_edge(entry->monitor, opposite(edge),
                                                  entry->position)) {
                            continue; // the target's own monitor continues there
                        }
                        const auto back = setup.arrangement.neighbour(
                            entry->device, entry->monitor, opposite(edge), entry->position);
                        ASSERT_TRUE(back.has_value());
                        EXPECT_EQ(back->device, device);
                        EXPECT_LE(std::abs(back->position.x - exit.x), kUnit);
                        EXPECT_LE(std::abs(back->position.y - exit.y), kUnit);
                        ++crossings;
                    }
                }
            }
        }
    }
    EXPECT_GT(crossings, 10'000U) << "the property was exercised";
}

TEST(LayoutCrossing, NoCrossingIntoGapsAndNoWallWhereAComputerTouches) {
    for (std::uint32_t seed = 1; seed <= 300; ++seed) {
        SCOPED_TRACE(seed);
        std::mt19937 random(seed);
        const RandomArrangement setup = random_arrangement(random);
        for (const auto& [device, screens] : setup.screens) {
            const Point offset = setup.placements.at(device).offset;
            for (std::size_t m = 0; m < screens.monitors().size(); ++m) {
                const Monitor& monitor = screens.monitors()[m];
                for (const Edge edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
                    for (const Point exit : along(monitor.native, edge, 11 * kUnit + 5)) {
                        if (!screens.at_outer_edge(m, edge, exit)) {
                            continue;
                        }
                        // The global point just past the edge, at the exit's position along it.
                        Point beyond = monitor.to_logical(exit) + offset;
                        const Rect frame = monitor.frame.moved(offset);
                        switch (edge) {
                        case Edge::Left:
                            beyond.x = frame.left() - 1;
                            break;
                        case Edge::Right:
                            beyond.x = frame.right();
                            break;
                        case Edge::Top:
                            beyond.y = frame.top() - 1;
                            break;
                        case Edge::Bottom:
                            beyond.y = frame.bottom();
                            break;
                        }
                        bool touching = false;
                        for (const auto& [other, other_screens] : setup.screens) {
                            if (other == device) {
                                continue;
                            }
                            for (const Rect& r : osdrop::layout::global_frames(
                                     other_screens, setup.placements.at(other))) {
                                touching = touching || r.contains(beyond);
                            }
                        }
                        const auto entry = setup.arrangement.neighbour(device, m, edge, exit);
                        if (touching) {
                            EXPECT_TRUE(entry.has_value()) << "a computer is right there";
                        }
                        if (entry) {
                            const Monitor& target =
                                setup.screens.at(entry->device).monitors()[entry->monitor];
                            const Rect global =
                                target.frame.moved(setup.placements.at(entry->device).offset);
                            const Point arrived = target.to_logical(entry->position) +
                                                  setup.placements.at(entry->device).offset;
                            EXPECT_TRUE(global.contains(arrived));
                            const bool vertical = edge == Edge::Left || edge == Edge::Right;
                            EXPECT_LE(
                                std::abs(vertical ? arrived.y - beyond.y : arrived.x - beyond.x),
                                kUnit)
                                << "same position along the edge";
                            EXPECT_LE(
                                std::abs(vertical ? arrived.x - beyond.x : arrived.y - beyond.y),
                                kSnapTolerance + kUnit);
                        }
                    }
                }
            }
        }
    }
}

TEST(LayoutCrossing, ExitsOnlyWhereTheOtherComputerSpansTheEdge) {
    // A tall Mac and a short PC on its right, aligned at the top: below the PC is a wall.
    Arrangement arrangement;
    arrangement.add(kMac, Screens({mac(1, units(0, 0, 1000, 1000), true)}), at(0, 0));
    arrangement.add(kPc, Screens({mac(1, units(0, 0, 800, 400), true)}), at(1000, 0));
    EXPECT_TRUE(arrangement.neighbour(kMac, 0, Edge::Right, at(999, 399)).has_value());
    EXPECT_FALSE(arrangement.neighbour(kMac, 0, Edge::Right, at(999, 400)).has_value());
    EXPECT_FALSE(arrangement.neighbour(kMac, 0, Edge::Left, at(0, 100)).has_value());
    // A detached or offline computer is not in the arrangement at all.
    const Arrangement without_pc =
        Arrangement::build({{kMac, {.offset = at(0, 0), .detached = false}},
                            {kPc, {.offset = at(1000, 0), .detached = true}}},
                           {{kMac, Screens({mac(1, units(0, 0, 1000, 1000), true)})},
                            {kPc, Screens({mac(1, units(0, 0, 800, 400), true)})}},
                           {kMac, kPc});
    EXPECT_FALSE(without_pc.neighbour(kMac, 0, Edge::Right, at(999, 100)).has_value());
}

TEST(LayoutCrossing, VerticalStacksAndLShapedDesktops) {
    // An L-shaped Mac desktop: the built-in display with an external one above its left half. A
    // PC sits on the right of the external monitor only.
    Arrangement arrangement;
    const Screens mac_desktop(
        {mac(1, units(0, 0, 1470, 956), true), mac(2, units(-500, -1440, 2560, 1440))});
    arrangement.add(kMac, mac_desktop, at(0, 0));
    arrangement.add(kPc, Screens({mac(1, units(0, 0, 1920, 1080), true)}), at(2060, -1440));
    arrangement.add(kLaptop, Screens({mac(1, units(0, 0, 1280, 800), true)}), at(100, 956));

    EXPECT_FALSE(mac_desktop.at_outer_edge(0, Edge::Top, at(100, 0))) << "the external monitor";
    EXPECT_TRUE(mac_desktop.at_outer_edge(1, Edge::Right, at(2059, -1000)));
    const auto to_pc = arrangement.neighbour(kMac, 1, Edge::Right, at(2059, -1000));
    ASSERT_TRUE(to_pc.has_value());
    EXPECT_EQ(to_pc->device, kPc);
    EXPECT_EQ(to_pc->position, at(0, 440));
    EXPECT_FALSE(arrangement.neighbour(kMac, 0, Edge::Right, at(1469, 500)).has_value())
        << "nothing on the right of the built-in display";

    const auto down = arrangement.neighbour(kMac, 0, Edge::Bottom, at(700, 955));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(down->device, kLaptop);
    EXPECT_EQ(down->position, at(600, 0));
    const auto up = arrangement.neighbour(kLaptop, 0, Edge::Top, down->position);
    ASSERT_TRUE(up.has_value());
    EXPECT_EQ(up->position, (Point{.x = 700 * kUnit, .y = (956 * kUnit) - 1}));
}

TEST(LayoutCrossing, EdgePushNeedsTimeAndDistanceWithoutPauses) {
    const EdgeContact edge{.device = kMac, .monitor = 0, .edge = Edge::Right};
    EdgePush push(
        PushConfig{.time = 1000ms, .distance = 32 * kUnit, .gap = 300ms, .immediate = false});
    const TimePoint start{};
    for (int ms = 0; ms < 1000; ms += 100) {
        EXPECT_FALSE(push.push(start + std::chrono::milliseconds(ms), edge, 4 * kUnit)) << ms;
    }
    EXPECT_NEAR(push.progress(start + 900ms), 0.9, 1e-9);
    EXPECT_TRUE(push.push(start + 1000ms, edge, 4 * kUnit));
    EXPECT_EQ(push.progress(start + 1000ms), 0.0) << "starts over";

    // A pause longer than the gap starts over; so does another edge.
    EXPECT_FALSE(push.push(start + 2000ms, edge, 40 * kUnit));
    EXPECT_FALSE(push.push(start + 2400ms, edge, 40 * kUnit));
    EXPECT_FALSE(push.push(start + 3300ms, edge, 40 * kUnit));
    EXPECT_FALSE(
        push.push(start + 3500ms, {.device = kMac, .monitor = 0, .edge = Edge::Top}, 40 * kUnit));
    EXPECT_EQ(push.progress(start + 3900ms), 0.0) << "lapsed";

    // Enough time but too little distance.
    EdgePush slow(
        PushConfig{.time = 1000ms, .distance = 32 * kUnit, .gap = 300ms, .immediate = false});
    for (int ms = 0; ms <= 1500; ms += 100) {
        EXPECT_FALSE(slow.push(start + std::chrono::milliseconds(ms), edge, 1 * kUnit)) << ms;
    }
    EXPECT_TRUE(slow.push(start + 1600ms, edge, 20 * kUnit));

    EdgePush immediate(
        PushConfig{.time = 1000ms, .distance = 32 * kUnit, .gap = 300ms, .immediate = true});
    EXPECT_TRUE(immediate.push(start, edge, 1));
}

/// A Mac on the left and a PC with a 150% laptop panel and a 100% monitor on its right.
struct MacAndPc {
    Screens mac_desktop{{mac(1, units(0, 0, 1470, 956), true)}};
    Screens pc_desktop =
        windows({{units(0, 0, 1920, 1200), 150}, {units(1920, 0, 2560, 1440), 100}});
    CrossingEngine engine{
        kMac, PushConfig{.time = 1000ms, .distance = 32 * kUnit, .gap = 300ms, .immediate = false}};

    MacAndPc() {
        Arrangement arrangement;
        arrangement.add(kMac, mac_desktop, at(0, 0));
        arrangement.add(kPc, pc_desktop, at(1470, 0));
        static_cast<void>(engine.set_arrangement(arrangement));
    }
    /// Pushes right at the Mac's edge until the cursor crosses; returns the time it did.
    CursorUpdate cross(TimePoint& now, std::int64_t y) {
        CursorUpdate update;
        for (int i = 0; i < 20 && !update.crossed; ++i) {
            now += 100ms;
            update = engine.motion(now, at(1469, y), at(10, 0));
        }
        return update;
    }
};

TEST(LayoutCrossing, TheEngineCrossesAfterAPushAndMovesTheRemoteCursorNatively) {
    MacAndPc setup;
    TimePoint now{};
    static_cast<void>(setup.engine.motion(now, at(1469, 300), at(10, 0)));
    const CursorUpdate pushing = setup.engine.motion(now + 100ms, at(1469, 300), at(10, 0));
    EXPECT_FALSE(pushing.crossed);
    EXPECT_EQ(pushing.device, kMac);
    EXPECT_GT(pushing.push, 0.0);
    EXPECT_EQ(setup.engine.motion(now + 150ms, at(1400, 300), at(-10, 0)).push, 0.0)
        << "moving away ends the push";

    const CursorUpdate crossed = setup.cross(now, 300);
    ASSERT_TRUE(crossed.crossed);
    EXPECT_EQ(crossed.device, kPc);
    EXPECT_EQ(crossed.position, at(0, 450)) << "300 points down is 450 pixels at 150%";
    EXPECT_TRUE(setup.engine.away());

    // 100 points of hand movement are 150 pixels on the laptop panel...
    now += 10ms;
    EXPECT_EQ(setup.engine.motion(now, at(700, 500), at(100, 0)).position, at(150, 450));
    // ...and continue onto the 100% monitor where the panel ends, as Windows moves the cursor.
    now += 10ms;
    const CursorUpdate onto = setup.engine.motion(now, at(700, 500), at(1200, 0));
    EXPECT_EQ(onto.position, at(150 + 1800, 450));
    // A wall at the PC's right edge: no neighbour there.
    now += 10ms;
    const CursorUpdate wall = setup.engine.motion(now, at(700, 500), at(5000, 0));
    EXPECT_EQ(wall.position, (Point{.x = ((1920 + 2560) * kUnit) - 1, .y = 450 * kUnit}));
    EXPECT_FALSE(wall.crossed);
    EXPECT_EQ(wall.push, 0.0);
}

TEST(LayoutCrossing, PointerSpeedAppliesPerComputer) {
    MacAndPc setup;
    setup.engine.set_speed(kPc, 2000);
    TimePoint now{};
    ASSERT_TRUE(setup.cross(now, 300).crossed);
    now += 10ms;
    EXPECT_EQ(setup.engine.motion(now, at(700, 500), at(100, 0)).position, at(300, 450));
}

TEST(LayoutCrossing, TheCursorComesBackAndGoesHomeWhenItsComputerLeaves) {
    MacAndPc setup;
    TimePoint now{};
    ASSERT_TRUE(setup.cross(now, 300).crossed);
    // Back through the PC's left edge: home, where it left.
    CursorUpdate update;
    for (int i = 0; i < 20 && !update.crossed; ++i) {
        now += 100ms;
        update = setup.engine.motion(now, at(700, 500), at(-10, 0));
    }
    ASSERT_TRUE(update.crossed);
    EXPECT_EQ(update.device, kMac);
    EXPECT_LE(std::abs(update.position.y - (300 * kUnit)), kUnit);
    EXPECT_FALSE(setup.engine.away());

    // Across again, then the PC drops out of the arrangement (offline, detached).
    ASSERT_TRUE(setup.cross(now, 200).crossed);
    Arrangement mac_only;
    mac_only.add(kMac, setup.mac_desktop, at(0, 0));
    const auto home = setup.engine.set_arrangement(mac_only);
    ASSERT_TRUE(home.has_value());
    EXPECT_EQ(home->device, kMac);
    EXPECT_EQ(home->position, at(1469, 200)) << "where it left";
    EXPECT_FALSE(setup.engine.away());

    EXPECT_FALSE(setup.cross(now, 100).crossed) << "nothing to cross to now";
}

TEST(LayoutCrossing, ReturnHomeComesBackToWhereTheCursorLeft) {
    MacAndPc setup;
    TimePoint now{};
    ASSERT_TRUE(setup.cross(now, 640).crossed);
    now += 10ms;
    static_cast<void>(setup.engine.motion(now, at(700, 500), at(400, 300)));
    const CursorUpdate home = setup.engine.return_home();
    EXPECT_TRUE(home.crossed);
    EXPECT_EQ(home.position, at(1469, 640));
    EXPECT_FALSE(setup.engine.return_home().crossed) << "already home";
}

} // namespace
