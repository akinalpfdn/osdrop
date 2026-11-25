// The layout document: last-writer-wins merges that converge in every order, the arrangement
// editor's checks, and where a computer that is not placed yet goes.
#include "layout/document.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace {

using osdrop::layout::check;
using osdrop::layout::DeviceId;
using osdrop::layout::follow_screens;
using osdrop::layout::kUnit;
using osdrop::layout::LayoutDocument;
using osdrop::layout::Monitor;
using osdrop::layout::Placement;
using osdrop::layout::Placements;
using osdrop::layout::Point;
using osdrop::layout::Problem;
using osdrop::layout::ProblemKind;
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

Screens one_monitor(std::int64_t width, std::int64_t height) {
    const Rect frame = units(0, 0, width, height);
    return Screens({Monitor{.key = {1},
                            .frame = frame,
                            .native = frame,
                            .pixel_width = 1,
                            .pixel_height = 1,
                            .primary = true}});
}

TEST(LayoutDocument, TheNewerRevisionWinsAndAuthorsBreakTies) {
    LayoutDocument document{.revision = 3, .author = kPc, .placements = {{kMac, {}}}};
    EXPECT_FALSE(document.merge({.revision = 2, .author = kLaptop, .placements = {}}));
    EXPECT_FALSE(document.merge({.revision = 3, .author = kMac, .placements = {}}));
    EXPECT_TRUE(document.merge({.revision = 3, .author = kLaptop, .placements = {{kPc, {}}}}));
    EXPECT_EQ(document.author, kLaptop);
    EXPECT_TRUE(document.placements.contains(kPc));
}

TEST(LayoutDocument, TheSameRevisionTwiceStillConverges) {
    const LayoutDocument a{.revision = 4, .author = kPc, .placements = {{kMac, {}}}};
    const LayoutDocument b{.revision = 4,
                           .author = kPc,
                           .placements = {{kMac, {.offset = at(5, 0), .detached = false}}}};
    LayoutDocument a_then_b = a;
    a_then_b.merge(b);
    LayoutDocument b_then_a = b;
    b_then_a.merge(a);
    EXPECT_EQ(a_then_b, b_then_a);
}

TEST(LayoutDocument, MergesConvergeInEveryOrder) {
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        SCOPED_TRACE(seed);
        std::mt19937 random(seed);
        constexpr std::size_t kMembers = 4;
        std::vector<LayoutDocument> replicas(kMembers);
        std::vector<LayoutDocument> written;
        for (int step = 0; step < 40; ++step) {
            const std::size_t member = random() % kMembers;
            if (random() % 2 == 0) { // an edit on top of what this member has
                LayoutDocument edit = replicas[member];
                edit.revision += 1;
                edit.author = DeviceId{static_cast<std::uint8_t>(member + 1)};
                edit.placements[DeviceId{static_cast<std::uint8_t>(random() % 6)}] = {
                    .offset = at(static_cast<std::int64_t>(random() % 3000), 0), .detached = false};
                replicas[member] = edit;
                written.push_back(edit);
            } else {
                replicas[member].merge(replicas[random() % kMembers]);
            }
        }
        std::vector<std::size_t> order(kMembers);
        for (std::size_t i = 0; i < kMembers; ++i) {
            order[i] = i;
        }
        std::ranges::shuffle(order, random);
        for (const std::size_t from : order) {
            for (const std::size_t to : order) {
                replicas[to].merge(replicas[from]);
            }
        }
        LayoutDocument newest;
        for (const LayoutDocument& edit : written) {
            newest.merge(edit);
        }
        for (const LayoutDocument& replica : replicas) {
            EXPECT_EQ(replica, newest);
        }
    }
}

TEST(LayoutDocument, RoundTripsThroughLayoutState) {
    const LayoutDocument document{
        .revision = 9,
        .author = kPc,
        .placements = {{kMac, {.offset = at(0, 0), .detached = false}},
                       {kPc, {.offset = at(1470, -100), .detached = false}},
                       {kLaptop, {.offset = at(-5000, 0), .detached = true}}}};
    const auto message = document.to_message();
    ASSERT_TRUE(osdrop::protocol::encode_control(message).has_value());
    EXPECT_EQ(LayoutDocument::from_message(message), document);
}

TEST(LayoutDocument, ChecksOverlapsAndComputersThatTouchNoOne) {
    const ScreenMap screens{{kMac, one_monitor(1470, 956)},
                            {kPc, one_monitor(2560, 1440)},
                            {kLaptop, one_monitor(1280, 800)}};
    Placements placements{{kMac, {.offset = at(0, 0), .detached = false}},
                          {kPc, {.offset = at(1470, -200), .detached = false}},
                          {kLaptop, {.offset = at(-1280, 100), .detached = false}}};
    EXPECT_TRUE(check(placements, screens).empty());

    placements[kPc].offset = at(1470 + 5, -200); // within the snap tolerance
    EXPECT_TRUE(check(placements, screens).empty());

    placements[kPc].offset = at(1400, -200);
    EXPECT_EQ(check(placements, screens),
              (std::vector<Problem>{{.kind = ProblemKind::Overlap, .device = kMac, .other = kPc}}));

    placements[kPc].offset = at(3000, 0); // floating on its own
    EXPECT_EQ(
        check(placements, screens),
        (std::vector<Problem>{{.kind = ProblemKind::Unreachable, .device = kPc, .other = {}}}));
    placements[kPc].detached = true;
    EXPECT_TRUE(check(placements, screens).empty()) << "detached computers are not checked";

    placements[kPc] = {.offset = at(1470, 956), .detached = false}; // touches at a corner only
    EXPECT_EQ(check(placements, screens).size(), 1U);
}

TEST(LayoutDocument, ANewComputerGoesToTheRightTouchingTheArrangement) {
    const ScreenMap screens{{kMac, one_monitor(1470, 956)}, {kPc, one_monitor(2560, 1440)}};
    const Placements placements{{kMac, {.offset = at(0, 0), .detached = false}},
                                {kPc, {.offset = at(1470, -200), .detached = false}}};
    const Screens laptop = one_monitor(1280, 800);
    const Point offset = osdrop::layout::place_beside(placements, screens, laptop);
    EXPECT_EQ(offset, at(1470 + 2560, -200));
    Placements with_laptop = placements;
    with_laptop[kLaptop] = {.offset = offset, .detached = false};
    ScreenMap all = screens;
    all[kLaptop] = laptop;
    EXPECT_TRUE(check(with_laptop, all).empty());
    EXPECT_EQ(osdrop::layout::place_beside({}, {}, laptop), Point{}) << "the first one";
}

/// A MacBook with `extra` virtual displays of 2560 × 1440 to its right, tops at -183 (as Osplay
/// adds them).
Screens mac_with_virtual(int extra) {
    std::vector<Monitor> monitors{Monitor{.key = {1},
                                          .frame = units(0, 0, 1710, 1107),
                                          .native = units(0, 0, 1710, 1107),
                                          .pixel_width = 3420,
                                          .pixel_height = 2214,
                                          .primary = true}};
    for (int i = 0; i < extra; ++i) {
        const Rect frame = units(1710 + (i * 2560), -183, 2560, 1440);
        monitors.push_back(Monitor{.key = {static_cast<std::uint8_t>(10 + i)},
                                   .frame = frame,
                                   .native = frame,
                                   .pixel_width = 2560,
                                   .pixel_height = 1440,
                                   .primary = false});
    }
    return Screens(std::move(monitors));
}

TEST(LayoutDocument, ComputersBesideMonitorsThatAppearMoveOutOfTheWayAndComeBack) {
    // The PC sits right of the MacBook and the laptop right of the PC; one sits below.
    const ScreenMap screens{{kMac, mac_with_virtual(0)},
                            {kPc, one_monitor(1920, 1080)},
                            {kLaptop, one_monitor(1470, 956)}};
    const DeviceId below{4};
    ScreenMap with_below = screens;
    with_below[below] = one_monitor(1000, 800);
    const Placements placements{{kMac, {.offset = at(0, 0)}},
                                {kPc, {.offset = at(1710, 0)}},
                                {kLaptop, {.offset = at(3630, 0)}},
                                {below, {.offset = at(0, 1107)}}};
    const Placements grown =
        follow_screens(placements, with_below, kMac, mac_with_virtual(0), mac_with_virtual(2));
    EXPECT_EQ(grown.at(kPc).offset, at(1710 + 5120, 0)) << "pushed by the two displays";
    EXPECT_EQ(grown.at(kLaptop).offset, at(3630 + 5120, 0)) << "and what lies beyond it";
    EXPECT_EQ(grown.at(below).offset, at(0, 1107)) << "the bottom edge did not move";
    EXPECT_EQ(grown.at(kMac).offset, at(0, 0));
    ScreenMap grown_screens = with_below;
    grown_screens[kMac] = mac_with_virtual(2);
    EXPECT_TRUE(check(grown, grown_screens).empty()) << "nothing overlaps";

    const Placements back =
        follow_screens(grown, grown_screens, kMac, mac_with_virtual(2), mac_with_virtual(0));
    EXPECT_EQ(back, placements) << "the displays went: everyone is back";
}

TEST(LayoutDocument, MonitorsAppearingOnTheLeftOrAboveMoveThoseComputersAway) {
    const Screens small = one_monitor(1000, 800);
    const auto monitor = [](std::uint8_t key, Rect frame) {
        return Monitor{.key = {key},
                       .frame = frame,
                       .native = frame,
                       .pixel_width = 500,
                       .pixel_height = 400,
                       .primary = false};
    };
    std::vector<Monitor> monitors = small.monitors();
    monitors.push_back(monitor(2, units(-500, 0, 500, 400)));    // left, across from the PC
    monitors.push_back(monitor(3, units(0, -400, 500, 400)));    // above, across from the laptop
    monitors.push_back(monitor(4, units(-900, -900, 400, 400))); // diagonal: across from nobody
    const Screens grown{std::move(monitors)};
    const ScreenMap screens{
        {kMac, small}, {kPc, one_monitor(300, 300)}, {kLaptop, one_monitor(300, 300)}};
    const Placements placements{{kMac, {.offset = at(0, 0)}},
                                {kPc, {.offset = at(-300, 0)}},
                                {kLaptop, {.offset = at(0, -300)}}};
    const Placements moved = follow_screens(placements, screens, kMac, small, grown);
    EXPECT_EQ(moved.at(kPc).offset, at(-800, 0)) << "left";
    EXPECT_EQ(moved.at(kLaptop).offset, at(0, -700)) << "above";
}

TEST(LayoutDocument, AComputerThatOverlappedStaysWhereItIs) {
    const ScreenMap screens{{kMac, mac_with_virtual(0)}, {kPc, one_monitor(1920, 1080)}};
    const Placements placements{{kMac, {.offset = at(0, 0)}}, {kPc, {.offset = at(100, 100)}}};
    EXPECT_EQ(follow_screens(placements, screens, kMac, mac_with_virtual(0), mac_with_virtual(1)),
              placements);
    EXPECT_EQ(
        follow_screens(placements, screens, kPc, one_monitor(1920, 1080), one_monitor(2000, 1080)),
        placements)
        << "and so does the computer whose screens changed";
}

} // namespace
