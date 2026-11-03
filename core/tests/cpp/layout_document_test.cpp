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

} // namespace
