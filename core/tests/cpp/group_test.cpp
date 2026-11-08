// Group membership over the simulated network (docs/PROTOCOL.md, "Group membership"): pairing
// creates or joins a group, members introduce new members to each other, revocations and leaving
// reach everyone including members that were offline, and local permissions refuse with specific
// codes.
#include "group_simulation.hpp"
#include "mesh_simulation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace {

namespace group = osdrop::group;
namespace protocol = osdrop::protocol;
using osdrop::Error;
using osdrop::test::build;
using osdrop::test::form_group;
using osdrop::test::knows;
using osdrop::test::pair;
using osdrop::test::rediscover;
using osdrop::test::SimulatedMeshNetwork;
using osdrop::test::SimulatedNode;
using osdrop::test::state;
using std::chrono::milliseconds;

void expect_full_mesh_of_members(SimulatedMeshNetwork& network) {
    for (std::size_t i = 0; i < network.size(); ++i) {
        EXPECT_EQ(state(network, i).roster.size(), network.size()) << "node " << i;
        EXPECT_EQ(state(network, i), state(network, 0)) << "node " << i;
        EXPECT_EQ(network.node(i).observer.connected.size(), network.size() - 1) << "node " << i;
    }
}

TEST(Group, PairingTwoComputersCreatesAGroupBothKeep) {
    SimulatedMeshNetwork network;
    build(network, 2);
    pair(network, 0, 1);
    ASSERT_TRUE(state(network, 0).in_group());
    EXPECT_EQ(state(network, 0), state(network, 1));
    EXPECT_EQ(state(network, 0).roster.size(), 2U);
    for (std::size_t i = 0; i < 2; ++i) {
        ASSERT_TRUE(network.node(i).group_observer.stored.has_value()) << "persisted";
        EXPECT_EQ(*network.node(i).group_observer.stored, state(network, i));
        EXPECT_EQ(network.node(i).observer.connected.size(), 1U);
    }
}

TEST(Group, NewMembersAreIntroducedAndTrustedWithoutAnotherCode) {
    SimulatedMeshNetwork network;
    form_group(network, 5);
    expect_full_mesh_of_members(network);
    for (std::size_t i = 0; i < network.size(); ++i) {
        EXPECT_LE(network.node(i).group_observer.codes.size(), 4U);
    }
    // Computers 1..4 each saw exactly one code: the one with computer 0.
    for (std::size_t i = 1; i < network.size(); ++i) {
        EXPECT_EQ(network.node(i).group_observer.codes.size(), 1U) << "node " << i;
    }
}

TEST(Group, ARevocationReachesEveryoneIncludingAMemberThatWasOffline) {
    SimulatedMeshNetwork network;
    form_group(network, 5);
    expect_full_mesh_of_members(network);
    const auto revoked = network.node(2).mesh.device_id();

    network.node(4).frozen = true; // offline while computer 2 is revoked
    ASSERT_TRUE(network.node(1).group->revoke(revoked, network.now).has_value());
    network.run_for(milliseconds(200));
    for (const std::size_t i : {0U, 1U, 3U}) {
        EXPECT_FALSE(knows(network, i, 2)) << "node " << i;
        EXPECT_FALSE(network.node(i).observer.connected.contains(revoked)) << "node " << i;
    }
    EXPECT_FALSE(state(network, 2).in_group()) << "computer 2 was told and left";
    ASSERT_EQ(network.node(2).group_observer.left.size(), 1U);
    EXPECT_EQ(network.node(2).group_observer.left.front(), Error::NotTrusted);

    // Computer 4 comes back with the stale roster and meets the others again.
    network.node(4).frozen = false;
    network.run_for(milliseconds(45'000));
    EXPECT_FALSE(knows(network, 4, 2)) << "it learned the revocation";
    for (const std::size_t i : {0U, 1U, 3U}) {
        EXPECT_FALSE(knows(network, i, 2)) << "the stale roster did not bring it back, node " << i;
        EXPECT_EQ(state(network, i), state(network, 4));
    }
    EXPECT_EQ(network.node(4).observer.connected.size(), 3U);
}

TEST(Group, ARevokedComputerIsRefusedEverywhere) {
    SimulatedMeshNetwork network;
    form_group(network, 3);
    const auto revoked = network.node(2).mesh.device_id();
    // Computer 2 misses its revocation (frozen), then tries to reconnect with its old roster.
    network.node(2).frozen = true;
    ASSERT_TRUE(network.node(0).group->revoke(revoked, network.now).has_value());
    network.run_for(milliseconds(100));
    network.node(2).frozen = false;
    network.run_for(milliseconds(15'000));
    EXPECT_FALSE(network.node(0).observer.connected.contains(revoked));
    EXPECT_FALSE(network.node(1).observer.connected.contains(revoked));
    EXPECT_TRUE(network.node(2).observer.connected.empty());
}

TEST(Group, ARemovedComputerPairsAgainWithACode) {
    SimulatedMeshNetwork network;
    form_group(network, 3);
    const auto removed = network.node(2).mesh.device_id();
    ASSERT_TRUE(network.node(0).group->revoke(removed, network.now).has_value());
    network.run_for(milliseconds(200));
    ASSERT_FALSE(state(network, 2).in_group());

    pair(network, 2, 1); // started on the removed computer, with a member that did not remove it
    rediscover(network);
    network.run_for(milliseconds(4'000));
    expect_full_mesh_of_members(network);
    EXPECT_EQ(network.node(0).group_observer.codes.size(), 2U) << "no code with the new pairing";
}

TEST(Group, TheMemberThatRemovedAComputerCanPairWithItAgain) {
    SimulatedMeshNetwork network;
    form_group(network, 2);
    const auto removed = network.node(1).mesh.device_id();
    ASSERT_TRUE(network.node(0).group->revoke(removed, network.now).has_value());
    network.run_for(milliseconds(200));
    ASSERT_FALSE(state(network, 1).in_group());

    pair(network, 0, 1);
    rediscover(network);
    network.run_for(milliseconds(4'000));
    expect_full_mesh_of_members(network);
}

/// Dials `to` from `from` to pair and reports whether `to` showed a code.
bool shows_code(SimulatedMeshNetwork& network, std::size_t from, std::size_t to,
                Error* failure = nullptr) {
    const std::size_t codes = network.node(to).group_observer.codes.size();
    const auto connection =
        network.node(from).mesh.pair_with(network.node(to).address, network.now);
    network.run_for(milliseconds(50));
    const auto& failures = network.node(from).group_observer.failures;
    if (failure != nullptr && !failures.empty() && failures.back().first == connection) {
        *failure = failures.back().second;
    }
    return network.node(to).group_observer.codes.size() > codes;
}

TEST(Group, ABlockedMemberIsRemovedAndRefusedBeforeAnyCodeEverywhere) {
    SimulatedMeshNetwork network;
    form_group(network, 3);
    const auto blocked = network.node(2).mesh.device_id();
    ASSERT_TRUE(network.node(0).group->block(blocked, network.now).has_value());
    network.run_for(milliseconds(200));
    ASSERT_FALSE(state(network, 2).in_group()) << "it was told and left";
    for (const std::size_t i : {0U, 1U}) {
        EXPECT_FALSE(knows(network, i, 2)) << "node " << i;
        EXPECT_TRUE(state(network, i).roster.is_blocked(blocked)) << "node " << i;
        Error failure = Error::Internal;
        EXPECT_FALSE(shows_code(network, 2, i, &failure)) << "node " << i;
        EXPECT_EQ(failure, Error::NotTrusted) << "node " << i;
    }
    const auto listed = network.node(1).group->blocked();
    ASSERT_EQ(listed.size(), 1U);
    EXPECT_EQ(listed.front().device_id, blocked);
}

TEST(Group, BlockingFromThePairingCodeRefusesItAndEveryMemberAfter) {
    SimulatedMeshNetwork network;
    build(network, 3);
    pair(network, 1, 0);
    rediscover(network);
    network.run_for(milliseconds(4'000));
    const auto stranger = network.node(2).mesh.device_id();
    EXPECT_EQ(network.node(1).group->block(stranger, network.now).error(), Error::InvalidArgument)
        << "nothing names it on computer 1";

    network.node(2).mesh.pair_with(network.node(0).address, network.now);
    network.run_for(milliseconds(50));
    ASSERT_EQ(network.node(0).group_observer.codes.size(), 2U); // the stranger's code is shown
    ASSERT_TRUE(network.node(0).group->block(stranger, network.now).has_value());
    network.run_for(milliseconds(200));
    EXPECT_FALSE(network.node(2).group_observer.failures.empty()) << "its pairing was refused";
    EXPECT_FALSE(state(network, 2).in_group());
    ASSERT_EQ(network.node(1).group->blocked().size(), 1U) << "the block reached computer 1";
    EXPECT_FALSE(network.node(1).group->blocked().front().name.empty()) << "named from its Hello";
    EXPECT_FALSE(shows_code(network, 2, 1));
    EXPECT_FALSE(shows_code(network, 2, 0));
}

TEST(Group, UnblockingLetsTheComputerPairAgain) {
    SimulatedMeshNetwork network;
    form_group(network, 3);
    const auto blocked = network.node(2).mesh.device_id();
    ASSERT_TRUE(network.node(0).group->block(blocked, network.now).has_value());
    network.run_for(milliseconds(200));
    EXPECT_EQ(network.node(1).group->unblock(network.node(0).mesh.device_id(), network.now).error(),
              Error::InvalidArgument)
        << "not blocked";
    ASSERT_TRUE(network.node(1).group->unblock(blocked, network.now).has_value());
    network.run_for(milliseconds(200));
    EXPECT_FALSE(state(network, 0).roster.is_blocked(blocked)) << "the unblock reached computer 0";

    pair(network, 2, 0);
    rediscover(network);
    network.run_for(milliseconds(4'000));
    expect_full_mesh_of_members(network);
}

TEST(Group, BlocksOutliveLeavingTheGroup) {
    SimulatedMeshNetwork network;
    form_group(network, 3);
    const auto blocked = network.node(2).mesh.device_id();
    ASSERT_TRUE(network.node(0).group->block(blocked, network.now).has_value());
    network.run_for(milliseconds(200));
    ASSERT_TRUE(network.node(1).group->leave(network.now).has_value());
    network.run_for(milliseconds(200));
    ASSERT_FALSE(state(network, 1).in_group());
    EXPECT_TRUE(state(network, 1).roster.is_blocked(blocked));
    EXPECT_FALSE(shows_code(network, 2, 1)) << "refused although computer 1 is in no group";
    EXPECT_EQ(network.node(1).group->block(network.node(1).mesh.device_id(), network.now).error(),
              Error::InvalidArgument)
        << "never itself";
}

TEST(Group, LeavingTellsTheOthersAndClearsTheState) {
    SimulatedMeshNetwork network;
    form_group(network, 3);
    ASSERT_TRUE(network.node(2).group->leave(network.now).has_value());
    network.run_for(milliseconds(200));
    EXPECT_FALSE(state(network, 2).in_group());
    ASSERT_EQ(network.node(2).group_observer.left.size(), 1U);
    EXPECT_EQ(network.node(2).group_observer.left.front(), std::nullopt);
    EXPECT_FALSE(knows(network, 0, 2));
    EXPECT_FALSE(knows(network, 1, 2));
    EXPECT_EQ(state(network, 0).roster.size(), 2U);
    network.run_for(milliseconds(5'000));
    EXPECT_TRUE(network.node(2).observer.connected.empty()) << "not dialled again";
}

TEST(Group, AComputerInAnotherGroupIsRefusedBeforeAnyCode) {
    SimulatedMeshNetwork network;
    build(network, 4);
    pair(network, 1, 0); // group of 0 and 1
    pair(network, 3, 2); // group of 2 and 3
    const auto connection = network.node(2).mesh.pair_with(network.node(0).address, network.now);
    network.run_for(milliseconds(50));
    EXPECT_EQ(network.node(0).group_observer.codes.size(), 1U) << "only its first pairing";
    ASSERT_FALSE(network.node(2).group_observer.failures.empty());
    EXPECT_EQ(network.node(2).group_observer.failures.back(),
              std::pair(connection, Error::InAnotherGroup));
}

TEST(Group, AFullGroupRefusesAnotherComputer) {
    SimulatedMeshNetwork network;
    build(network, group::kMaxGroupSize + 1);
    for (std::size_t i = 1; i < group::kMaxGroupSize; ++i) {
        pair(network, i, 0);
    }
    ASSERT_EQ(state(network, 0).roster.size(), group::kMaxGroupSize);
    const std::size_t extra = group::kMaxGroupSize;
    const auto connection =
        network.node(extra).mesh.pair_with(network.node(0).address, network.now);
    network.run_for(milliseconds(50));
    ASSERT_FALSE(network.node(extra).group_observer.failures.empty());
    EXPECT_EQ(network.node(extra).group_observer.failures.back(),
              std::pair(connection, Error::GroupFull));
}

TEST(Group, OnlyOnePairingCodeIsShownAtATime) {
    SimulatedMeshNetwork network;
    build(network, 3);
    network.node(1).mesh.pair_with(network.node(0).address, network.now);
    network.run_for(milliseconds(50));
    ASSERT_EQ(network.node(0).group_observer.codes.size(), 1U); // waiting for the user
    const auto second = network.node(2).mesh.pair_with(network.node(0).address, network.now);
    network.run_for(milliseconds(50));
    EXPECT_EQ(network.node(0).group_observer.codes.size(), 1U);
    ASSERT_FALSE(network.node(2).group_observer.failures.empty());
    EXPECT_EQ(network.node(2).group_observer.failures.back(), std::pair(second, Error::PeerBusy));
}

TEST(Group, PermissionsRefuseWhatTheyDoNotAllowWithSpecificCodes) {
    SimulatedMeshNetwork network;
    network.control_layer = false; // refusals reach the observer instead of the input control
    form_group(network, 2);
    SimulatedNode& here = network.node(0);
    SimulatedNode& peer = network.node(1);
    const auto here_id = here.mesh.device_id();
    const auto peer_id = peer.mesh.device_id();
    ASSERT_TRUE(here.group
                    ->set_permissions(
                        peer_id, {.control = false, .files = true, .clipboard = false}, network.now)
                    .has_value());
    network.run_for(milliseconds(10));

    // Control: Enter is refused with NOT_PERMITTED and never reaches the layers above.
    ASSERT_TRUE(
        peer.mesh
            .send_message(here_id, protocol::Enter{.control_id = 5, .x = 0, .y = 0}, network.now)
            .has_value());
    network.run_for(milliseconds(10));
    EXPECT_TRUE(here.observer.messages.empty());
    ASSERT_EQ(peer.observer.messages.size(), 1U);
    EXPECT_EQ(peer.observer.messages.back().second,
              protocol::ControlMessage(protocol::Refuse{
                  .control_id = 5, .reason = protocol::RefuseReason::NotPermitted}));

    // Clipboard: announcements are dropped, requests answered with NOT_PERMITTED.
    ASSERT_TRUE(peer.mesh
                    .send_message(here_id,
                                  protocol::ClipboardAnnounce{
                                      .clip_id = 1,
                                      .formats = {{.format = protocol::ClipboardFormat::PlainText,
                                                   .byte_count = 3}}},
                                  network.now)
                    .has_value());
    const auto stream = peer.mesh.open_transfer(here_id, network.now);
    ASSERT_TRUE(stream.has_value());
    network.run_for(milliseconds(10));
    ASSERT_TRUE(peer.mesh
                    .send_transfer(
                        here_id, stream.value(),
                        protocol::ContentRequest{.request_id = 4,
                                                 .object = {.kind = protocol::ObjectKind::Clipboard,
                                                            .object_id = 1,
                                                            .item_index = 1},
                                                 .entry_index = 0,
                                                 .offset = 0,
                                                 .length = 3},
                        network.now)
                    .has_value());
    network.run_for(milliseconds(10));
    EXPECT_TRUE(here.observer.messages.empty());
    EXPECT_TRUE(here.observer.transfer_messages.empty());
    ASSERT_EQ(peer.observer.transfer_messages.size(), 1U);
    EXPECT_EQ(peer.observer.transfer_messages.back().second,
              protocol::TransferMessage(protocol::TransferError{
                  .request_id = 4, .code = static_cast<std::uint16_t>(Error::NotPermitted)}));

    // The peer sees what it may no longer do in the features this computer announces.
    ASSERT_TRUE(here.group
                    ->set_permissions(peer_id, {.control = true, .files = false, .clipboard = true},
                                      network.now)
                    .has_value());
    network.run_for(milliseconds(10));
    // Files: a drag carried across with Enter is refused; plain control is fine again.
    ASSERT_TRUE(peer.mesh
                    .send_message(here_id,
                                  protocol::Enter{.control_id = 6, .x = 0, .y = 0, .drag_id = 9},
                                  network.now)
                    .has_value());
    ASSERT_TRUE(
        peer.mesh
            .send_message(here_id, protocol::Enter{.control_id = 7, .x = 0, .y = 0}, network.now)
            .has_value());
    network.run_for(milliseconds(10));
    ASSERT_EQ(here.observer.messages.size(), 1U);
    EXPECT_EQ(std::get<protocol::Enter>(here.observer.messages.back().second).control_id, 7U);
    EXPECT_EQ(peer.observer.messages.back().second,
              protocol::ControlMessage(protocol::Refuse{
                  .control_id = 6, .reason = protocol::RefuseReason::NotPermitted}));
}

TEST(Group, TheStoredStateRestoresTrustAfterARestart) {
    std::vector<group::GroupState> saved;
    {
        SimulatedMeshNetwork first;
        form_group(first, 3);
        for (std::size_t i = 0; i < 3; ++i) {
            const auto stored = first.node(i).group_observer.stored;
            ASSERT_TRUE(stored.has_value());
            const auto decoded = group::decode_state(group::encode_state(*stored));
            ASSERT_TRUE(decoded.has_value());
            saved.push_back(decoded.value());
        }
    }
    SimulatedMeshNetwork second;
    for (std::size_t i = 0; i < 3; ++i) { // same identities (seeds), restored states
        second.add(SimulatedMeshNetwork::config(saved[i].group_id, "Computer " + std::to_string(i)),
                   saved[i], 7 + i);
    }
    rediscover(second);
    second.run_for(milliseconds(4'000));
    expect_full_mesh_of_members(second);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_TRUE(second.node(i).group_observer.codes.empty()) << "no code after a restart";
    }
}

} // namespace
