// The roster's merge rules (docs/PROTOCOL.md, "Group membership"), including property tests:
// replicas that apply random pairings and revocations and merge in random orders always converge,
// and a revocation is never undone by a stale replica.
#include "crypto/noise.hpp"
#include "group/group_state.hpp"
#include "group/roster.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace {

namespace crypto = osdrop::crypto;
namespace protocol = osdrop::protocol;
using osdrop::group::DeviceId;
using osdrop::group::Member;
using osdrop::group::Roster;

/// A member entry with a consistent device ID; keys need not be real X25519 keys here.
Member member(std::uint8_t seed, std::uint32_t epoch = 1, const char* name = "Computer") {
    crypto::Key key{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        key.at(i) = static_cast<std::uint8_t>(seed * 31 + i);
    }
    return Member{.device_id = crypto::device_id(key),
                  .public_key = key,
                  .epoch = epoch,
                  .platform = protocol::Platform::MacOS,
                  .name = name};
}

TEST(Roster, PairingRevokingAndPairingAgain) {
    Roster roster;
    const Member pc = member(1);
    EXPECT_TRUE(roster.add(pc));
    EXPECT_FALSE(roster.add(pc)) << "nothing new";
    EXPECT_TRUE(roster.is_member(pc.device_id));
    EXPECT_EQ(roster.size(), 1U);

    EXPECT_TRUE(roster.revoke(pc.device_id, 1));
    EXPECT_FALSE(roster.is_member(pc.device_id));
    EXPECT_EQ(roster.size(), 0U);
    EXPECT_EQ(roster.next_epoch(pc.device_id), 2U);

    EXPECT_FALSE(roster.add(pc)) << "the revoked pairing stays revoked";
    EXPECT_FALSE(roster.is_member(pc.device_id));
    EXPECT_TRUE(roster.add(member(1, 2)));
    EXPECT_TRUE(roster.is_member(pc.device_id)) << "a new pairing brings it back";
}

TEST(Roster, EntriesWhoseIdIsNotTheKeysAreRefused) {
    Roster roster;
    Member forged = member(1);
    forged.device_id = member(2).device_id;
    EXPECT_FALSE(roster.add(forged));
    EXPECT_FALSE(roster.is_member(forged.device_id));

    protocol::RosterState state{.group_id = {1},
                                .members = {{.device_id = forged.device_id,
                                             .public_key = forged.public_key,
                                             .epoch = 1,
                                             .platform = protocol::Platform::MacOS,
                                             .name = "Forged"}},
                                .revocations = {}};
    EXPECT_FALSE(Roster::from_message(state).has_value());
}

TEST(Roster, AStaleReplicaCannotBringARevokedDeviceBack) {
    Roster up_to_date;
    up_to_date.add(member(1));
    up_to_date.add(member(2));
    Roster stale = up_to_date; // offline while the revocation happens
    up_to_date.revoke(member(2).device_id, 1);

    EXPECT_FALSE(up_to_date.merge(stale));
    EXPECT_FALSE(up_to_date.is_member(member(2).device_id));
    EXPECT_TRUE(stale.merge(up_to_date));
    EXPECT_FALSE(stale.is_member(member(2).device_id));
    EXPECT_EQ(stale, up_to_date);
}

TEST(Roster, NamesOfTheSamePairingConvergeToOneChoice) {
    Roster a;
    Roster b;
    a.add(member(1, 1, "Mac"));
    b.add(member(1, 1, "MacBook"));
    Roster a_then_b = a;
    a_then_b.merge(b);
    Roster b_then_a = b;
    b_then_a.merge(a);
    EXPECT_EQ(a_then_b, b_then_a);
}

TEST(Roster, MessagesSplitToFitAndRebuildTheSameRoster) {
    Roster roster;
    for (std::uint8_t i = 0; i < 100; ++i) {
        roster.add(member(i));
    }
    for (std::uint8_t i = 0; i < 40; ++i) {
        roster.revoke(member(i).device_id, 1);
    }
    for (std::uint16_t i = 0; i < 300; ++i) { // devices revoked long ago, never paired here
        DeviceId device{};
        device.at(0) = static_cast<std::uint8_t>(i);
        device.at(1) = static_cast<std::uint8_t>(i >> 8U);
        device.at(15) = 0xEE;
        roster.revoke(device, 3);
    }
    const auto messages = roster.to_messages(protocol::Id{7});
    ASSERT_EQ(messages.size(), 2U); // 100 members need 2, 340 revocations need 2
    Roster rebuilt;
    for (const auto& message : messages) {
        ASSERT_TRUE(protocol::encode_control(message).has_value()) << "fits the message rules";
        const auto part = Roster::from_message(message);
        ASSERT_TRUE(part.has_value());
        rebuilt.merge(part.value());
    }
    EXPECT_EQ(rebuilt, roster);
    EXPECT_EQ(rebuilt.size(), 60U);
}

// Several replicas apply random pairings and revocations and merge with random partners in
// random order; once everyone has merged with everyone, all are equal, and membership agrees with
// a reference computed from every operation at once.
TEST(Roster, ReplicasConvergeForEveryOrderOfOperationsAndMerges) {
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        SCOPED_TRACE(seed);
        std::mt19937 random(seed);
        constexpr std::size_t kReplicas = 5;
        constexpr std::uint8_t kDevices = 6;
        std::vector<Roster> replicas(kReplicas);
        Roster reference;
        for (int step = 0; step < 60; ++step) {
            const std::size_t replica = random() % kReplicas;
            const auto device = static_cast<std::uint8_t>(random() % kDevices);
            switch (random() % 3) {
            case 0: { // pair (again) with the next epoch this replica knows of
                const Member entry =
                    member(device, replicas[replica].next_epoch(member(device).device_id));
                replicas[replica].add(entry);
                reference.add(entry);
                break;
            }
            case 1: // revoke what this replica sees as the device's pairing
                if (const auto current = replicas[replica].member(member(device).device_id)) {
                    replicas[replica].revoke(current->device_id, current->epoch);
                    reference.revoke(current->device_id, current->epoch);
                }
                break;
            default: { // gossip between two replicas
                const std::size_t other = random() % kReplicas;
                replicas[replica].merge(replicas[other]);
                break;
            }
            }
        }
        std::vector<std::size_t> order(kReplicas);
        for (std::size_t i = 0; i < kReplicas; ++i) {
            order[i] = i;
        }
        std::ranges::shuffle(order, random);
        for (const std::size_t from : order) {
            for (const std::size_t to : order) {
                replicas[to].merge(replicas[from]);
            }
        }
        for (const Roster& replica : replicas) {
            EXPECT_EQ(replica, replicas.front());
        }
        EXPECT_EQ(replicas.front(), reference);
        EXPECT_FALSE(replicas.front().merge(reference)) << "merging is idempotent";
    }
}

TEST(GroupState, RoundTripsThroughItsEncoding) {
    osdrop::group::GroupState state;
    state.group_id = protocol::Id{9, 8, 7};
    state.roster.add(member(1, 1, "Mac"));
    state.roster.add(member(2, 3, "Çalışma PC"));
    state.roster.revoke(member(3).device_id, 2);
    state.permissions[member(2).device_id] = {.control = false, .files = true, .clipboard = false};
    const auto bytes = osdrop::group::encode_state(state);
    const auto decoded = osdrop::group::decode_state(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), state);

    const auto empty =
        osdrop::group::decode_state(osdrop::group::encode_state(osdrop::group::GroupState{}));
    ASSERT_TRUE(empty.has_value());
    EXPECT_FALSE(empty.value().in_group());
}

TEST(GroupState, DamagedOrNewerStatesAreRefused) {
    osdrop::group::GroupState state;
    state.group_id = protocol::Id{1};
    state.roster.add(member(1));
    const auto good = osdrop::group::encode_state(state);
    using osdrop::Error;
    const auto decode = [](std::vector<std::uint8_t> bytes) {
        const auto result = osdrop::group::decode_state(bytes);
        return result ? Error::Internal : result.error();
    };

    auto newer = good;
    newer[4] = 2;
    EXPECT_EQ(decode(newer), Error::UnsupportedVersion) << "kept, never overwritten";
    auto foreign = good;
    foreign[0] = 'X';
    EXPECT_EQ(decode(foreign), Error::BadMagic);
    EXPECT_EQ(decode({good.begin(), good.end() - 1}), Error::Truncated);
    auto longer = good;
    longer.push_back(0);
    EXPECT_EQ(decode(longer), Error::TrailingBytes);
    auto forged = good;
    forged[4 + 1 + 16 + 2] ^= 1U; // the member's device ID no longer matches its key
    EXPECT_EQ(decode(forged), Error::ValueOutOfRange);
}

TEST(GroupState, PermissionsDecideTheOfferedFeatures) {
    using osdrop::group::offered_features;
    EXPECT_EQ(offered_features({}), protocol::kKnownFeatures);
    EXPECT_EQ(offered_features({.control = false, .files = true, .clipboard = true}) &
                  protocol::kFeatureTakesControl,
              0U);
    EXPECT_EQ(offered_features({.control = true, .files = false, .clipboard = true}) &
                  (protocol::kFeatureDragTarget | protocol::kFeatureFiles),
              0U);
    EXPECT_EQ(offered_features({.control = true, .files = true, .clipboard = false}) &
                  protocol::kFeatureClipboard,
              0U);
}

} // namespace
