#pragma once

// Groups over the simulated network: computers that pair, discover each other and settle into
// one group, for the group and layout tests.

#include "mesh_simulation.hpp"

#include <gtest/gtest.h>

#include <string>

namespace osdrop::test {

/// Computers in no group yet.
inline void build(SimulatedMeshNetwork& network, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        network.add(SimulatedMeshNetwork::config(protocol::Id{}, "Computer " + std::to_string(i)),
                    group::GroupState{});
    }
}

/// `from` pairs with `to`; both users confirm the code.
inline void pair(SimulatedMeshNetwork& network, std::size_t from, std::size_t to) {
    SimulatedNode& a = network.node(from);
    SimulatedNode& b = network.node(to);
    const std::size_t a_codes = a.group_observer.codes.size();
    const std::size_t b_codes = b.group_observer.codes.size();
    const auto connection = a.mesh.pair_with(b.address, network.now);
    for (int i = 0; i < 100 && (a.group_observer.codes.size() == a_codes ||
                                b.group_observer.codes.size() == b_codes);
         ++i) {
        network.step();
    }
    ASSERT_GT(a.group_observer.codes.size(), a_codes) << "no code on " << from;
    ASSERT_GT(b.group_observer.codes.size(), b_codes) << "no code on " << to;
    ASSERT_EQ(a.group_observer.codes.back().second, b.group_observer.codes.back().second);
    ASSERT_TRUE(a.mesh.confirm_pairing(connection, true, network.now).has_value());
    ASSERT_TRUE(
        b.mesh.confirm_pairing(b.group_observer.codes.back().first, true, network.now).has_value());
    network.run_for(milliseconds(50));
}

/// Every computer sees every other one's current advertisement.
inline void rediscover(SimulatedMeshNetwork& network) {
    for (std::size_t i = 0; i < network.size(); ++i) {
        for (std::size_t j = 0; j < network.size(); ++j) {
            if (i != j) {
                network.node(i).mesh.peer_discovered(network.node(j).mesh.device_id(),
                                                     network.node(j).group->state().group_id,
                                                     network.node(j).address, network.now);
            }
        }
    }
}

inline const group::GroupState& state(SimulatedMeshNetwork& network, std::size_t node) {
    return network.node(node).group->state();
}

inline bool knows(SimulatedMeshNetwork& network, std::size_t node, std::size_t other) {
    return state(network, node).roster.is_member(network.node(other).mesh.device_id());
}

/// Pairs computers 1..count-1 with computer 0, one after another, and lets the group settle.
inline void form_group(SimulatedMeshNetwork& network, std::size_t count) {
    build(network, count);
    for (std::size_t i = 1; i < count; ++i) {
        pair(network, i, 0);
        rediscover(network);
        network.run_for(milliseconds(4'000)); // the higher device ID's fallback dial included
    }
}

} // namespace osdrop::test
