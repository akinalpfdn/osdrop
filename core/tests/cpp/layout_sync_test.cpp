// The shared arrangement over the simulated network (docs/PROTOCOL.md, "Arrangement"): members
// report their screens and place themselves, edits reach everyone including members that were
// offline, concurrent edits converge to the newest, and the stored layout survives a restart but
// not a change of group.
#include "group_simulation.hpp"
#include "layout/layout_state.hpp"
#include "mesh_simulation.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

namespace group = osdrop::group;
namespace layout = osdrop::layout;
namespace protocol = osdrop::protocol;
using layout::kUnit;
using osdrop::Error;
using osdrop::test::form_group;
using osdrop::test::pair;
using osdrop::test::rediscover;
using osdrop::test::SimulatedMeshNetwork;
using osdrop::test::SimulatedNode;
using std::chrono::milliseconds;

layout::Screens one_monitor(std::uint8_t key, std::int64_t width, std::int64_t height) {
    const layout::Rect frame{.x = 0, .y = 0, .width = width * kUnit, .height = height * kUnit};
    return layout::Screens({layout::Monitor{.key = {key},
                                            .frame = frame,
                                            .native = frame,
                                            .pixel_width = 1,
                                            .pixel_height = 1,
                                            .primary = true}});
}

const layout::LayoutState& layout_of(SimulatedMeshNetwork& network, std::size_t node) {
    return network.node(node).layout->state();
}

layout::DeviceId id_of(SimulatedMeshNetwork& network, std::size_t node) {
    return network.node(node).mesh.device_id();
}

/// Every member reports a screen of its own size.
void report_screens(SimulatedMeshNetwork& network) {
    for (std::size_t i = 0; i < network.size(); ++i) {
        const auto width = static_cast<std::int64_t>(1200 + (i * 300));
        ASSERT_TRUE(network.node(i)
                        .layout
                        ->set_screens(one_monitor(static_cast<std::uint8_t>(i + 1), width, 900),
                                      network.now)
                        .has_value());
    }
    network.run_for(milliseconds(200));
}

/// The computer placed furthest right: computers place themselves in a row, so detaching it
/// leaves the others touching.
layout::DeviceId rightmost(const layout::Placements& placements) {
    auto best = placements.begin();
    for (auto it = placements.begin(); it != placements.end(); ++it) {
        if (it->second.offset.x > best->second.offset.x) {
            best = it;
        }
    }
    return best->first;
}

void expect_one_arrangement(SimulatedMeshNetwork& network) {
    for (std::size_t i = 0; i < network.size(); ++i) {
        EXPECT_EQ(layout_of(network, i).document, layout_of(network, 0).document) << "node " << i;
        EXPECT_EQ(layout_of(network, i).screens, layout_of(network, 0).screens) << "node " << i;
        ASSERT_TRUE(network.node(i).layout_observer.stored.has_value());
        EXPECT_EQ(*network.node(i).layout_observer.stored, layout_of(network, i)) << "persisted";
    }
}

TEST(LayoutSync, MembersShareScreensAndPlaceThemselvesSideBySide) {
    SimulatedMeshNetwork network;
    form_group(network, 3);
    report_screens(network);
    expect_one_arrangement(network);
    const layout::LayoutState& shared = layout_of(network, 0);
    EXPECT_EQ(shared.screens.size(), 3U);
    ASSERT_EQ(shared.document.placements.size(), 3U);
    EXPECT_TRUE(layout::check(shared.document.placements, shared.screens).empty())
        << "every computer touches the arrangement, nothing overlaps";
    EXPECT_EQ(shared.group_id, network.node(0).group->state().group_id);
}

TEST(LayoutSync, AnEditReachesEveryoneIncludingAMemberThatWasOffline) {
    SimulatedMeshNetwork network;
    form_group(network, 3);
    report_screens(network);

    // The computer on the far right is detached while computer 1 is offline.
    network.node(1).frozen = true;
    layout::Placements edit = layout_of(network, 0).document.placements;
    edit[rightmost(edit)].detached = true;
    ASSERT_TRUE(network.node(0).layout->set_placements(edit, network.now).has_value());
    network.run_for(milliseconds(200));
    EXPECT_EQ(layout_of(network, 2).document.placements, edit);

    network.node(1).frozen = false;
    network.run_for(milliseconds(15'000));
    expect_one_arrangement(network);
    EXPECT_EQ(layout_of(network, 1).document.placements, edit);
}

TEST(LayoutSync, ConcurrentEditsConvergeToTheNewest) {
    SimulatedMeshNetwork network;
    form_group(network, 3);
    report_screens(network);
    const layout::Placements base = layout_of(network, 0).document.placements;
    layout::Placements by_0 = base;
    by_0[rightmost(base)].detached = true;
    const layout::Placements& by_2 = base;
    ASSERT_TRUE(network.node(0).layout->set_placements(by_0, network.now).has_value());
    ASSERT_TRUE(network.node(2).layout->set_placements(by_2, network.now).has_value());
    network.run_for(milliseconds(500));
    expect_one_arrangement(network);
    const auto& winner = layout_of(network, 0).document;
    EXPECT_EQ(winner.author, std::max(id_of(network, 0), id_of(network, 2)))
        << "the same revision: the higher author wins";
}

TEST(LayoutSync, InvalidArrangementsAreRefused) {
    SimulatedMeshNetwork network;
    form_group(network, 2);
    report_screens(network);
    const layout::Placements before = layout_of(network, 0).document.placements;

    layout::Placements overlapping = before;
    overlapping[id_of(network, 1)].offset = {.x = 100 * kUnit, .y = 0};
    EXPECT_EQ(network.node(0).layout->set_placements(overlapping, network.now).error(),
              Error::InvalidLayout);
    layout::Placements stranger = before;
    stranger[layout::DeviceId{0xEE}] = {};
    EXPECT_EQ(network.node(0).layout->set_placements(stranger, network.now).error(),
              Error::InvalidArgument);
    EXPECT_EQ(network.node(0).layout->set_screens(layout::Screens{}, network.now).error(),
              Error::InvalidArgument);
    EXPECT_EQ(layout_of(network, 0).document.placements, before);
}

TEST(LayoutSync, ANewMemberPlacesItselfBesideTheArrangement) {
    SimulatedMeshNetwork network;
    form_group(network, 2);
    report_screens(network);
    // A third computer pairs, reports its screen and joins the arrangement on its own.
    network.add(SimulatedMeshNetwork::config(protocol::Id{}, "Computer 2"), group::GroupState{});
    ASSERT_TRUE(
        network.node(2).layout->set_screens(one_monitor(3, 1600, 1000), network.now).has_value());
    pair(network, 2, 0);
    rediscover(network);
    network.run_for(milliseconds(5'000));
    expect_one_arrangement(network);
    const layout::LayoutState& shared = layout_of(network, 0);
    EXPECT_EQ(shared.document.placements.size(), 3U);
    EXPECT_TRUE(layout::check(shared.document.placements, shared.screens).empty());
}

TEST(LayoutSync, TheStoredLayoutSurvivesARestartButNotAnotherGroup) {
    SimulatedMeshNetwork network;
    form_group(network, 2);
    report_screens(network);
    const layout::LayoutState stored = *network.node(0).layout_observer.stored;

    const auto decoded = layout::decode_layout(layout::encode_layout(stored));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), stored);

    // The same computer in the same group keeps it; in another group it starts over.
    SimulatedMeshNetwork again;
    SimulatedNode& node = again.add(SimulatedMeshNetwork::config(stored.group_id, "Computer 0"),
                                    network.node(0).group->state(), 7, decoded.value());
    EXPECT_EQ(node.layout->state(), stored);
    node.layout->group_changed(protocol::Id{0x42}, {});
    const auto& placements = node.layout->state().document.placements;
    EXPECT_EQ(placements.size(), 1U) << "only this computer, placed again";
    EXPECT_TRUE(placements.contains(node.mesh.device_id()));
    EXPECT_EQ(node.layout->state().group_id, protocol::Id{0x42});
    EXPECT_EQ(node.layout->state().screens.size(), 1U) << "its own screens remain";
}

TEST(LayoutState, DamagedOrNewerStatesAreRefused) {
    layout::LayoutState state;
    state.group_id = protocol::Id{1};
    state.screens[layout::DeviceId{2}] = one_monitor(1, 1470, 956);
    state.document = {.revision = 3,
                      .author = layout::DeviceId{2},
                      .placements = {{layout::DeviceId{2}, {.offset = {}, .detached = false}}}};
    const auto good = layout::encode_layout(state);
    const auto decode = [](std::vector<std::uint8_t> bytes) {
        const auto result = layout::decode_layout(bytes);
        return result ? Error::Internal : result.error();
    };
    auto newer = good;
    newer[4] = 2;
    EXPECT_EQ(decode(newer), Error::UnsupportedVersion);
    auto foreign = good;
    foreign[1] = 'X';
    EXPECT_EQ(decode(foreign), Error::BadMagic);
    EXPECT_EQ(decode({good.begin(), good.end() - 1}), Error::Truncated);
    auto longer = good;
    longer.push_back(0);
    EXPECT_EQ(decode(longer), Error::TrailingBytes);
    const auto empty = layout::decode_layout(layout::encode_layout({}));
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(empty.value(), layout::LayoutState{});
}

} // namespace
