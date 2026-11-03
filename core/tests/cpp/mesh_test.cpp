#include "mesh_simulation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

namespace {

namespace protocol = osdrop::protocol;
using osdrop::Error;
using osdrop::Microseconds;
using osdrop::test::Endpoint;
using osdrop::test::SimulatedMeshNetwork;
using std::chrono::milliseconds;

constexpr protocol::Id kGroup{0x47, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

/// Must never appear on the wire: it only travels inside encrypted messages.
const std::string kMarker = "OSDROP-PLAINTEXT-MARKER";

std::size_t build(SimulatedMeshNetwork& network, std::size_t count,
                  const protocol::Id& group = kGroup,
                  Microseconds fallback = Microseconds{3'000'000}) {
    for (std::size_t i = 0; i < count; ++i) {
        auto config = SimulatedMeshNetwork::config(group, "Computer " + std::to_string(i));
        config.tunables.dial_fallback_delay = fallback;
        network.add(config);
    }
    return count;
}

/// The node with the lower device ID first.
std::pair<std::size_t, std::size_t> ordered_pair(SimulatedMeshNetwork& network) {
    return network.node(0).mesh.device_id() < network.node(1).mesh.device_id() ? std::pair(0, 1)
                                                                               : std::pair(1, 0);
}

bool contains(const std::vector<std::uint8_t>& haystack, const std::string& needle) {
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) !=
           haystack.end();
}

void expect_full_mesh(SimulatedMeshNetwork& network) {
    const std::size_t count = network.size();
    for (std::size_t i = 0; i < count; ++i) {
        EXPECT_EQ(network.node(i).observer.connected.size(), count - 1) << "node " << i;
        EXPECT_EQ(network.node(i).mesh.connected_peers().size(), count - 1) << "node " << i;
    }
    EXPECT_EQ(network.open_links().size(), count * (count - 1) / 2)
        << "exactly one connection per pair";
}

TEST(Mesh, FiveMembersConvergeToAFullMesh) {
    SimulatedMeshNetwork network;
    build(network, 5);
    network.discover_all(kGroup);
    network.run_for(milliseconds(500));
    expect_full_mesh(network);
    for (std::size_t i = 0; i < network.size(); ++i) {
        EXPECT_TRUE(network.node(i).observer.disconnects.empty()) << "no connection was replaced";
    }
}

TEST(Mesh, FiveMembersConvergeWhenEveryMemberDialsAtOnce) {
    SimulatedMeshNetwork network;
    build(network, 5, kGroup, Microseconds{0}); // no fallback delay: both sides dial at once
    network.discover_all(kGroup);
    network.run_for(milliseconds(1'000));
    expect_full_mesh(network);
    // Both sides of every pair kept the same connection: each open link is one live session.
    for (const auto& [a, b] : network.open_links()) {
        EXPECT_TRUE(network.node(a.node).mesh.is_connected(network.node(b.node).mesh.device_id()));
    }
    network.run_for(milliseconds(2'000));
    expect_full_mesh(network);
}

TEST(Mesh, MembersOfOtherGroupsAndComputersWithoutAGroupAreNotDialled) {
    SimulatedMeshNetwork network;
    network.add(SimulatedMeshNetwork::config(kGroup, "Member"));
    network.add(SimulatedMeshNetwork::config(protocol::Id{9}, "Other group"));
    network.add(SimulatedMeshNetwork::config(protocol::Id{}, "No group"));
    network.node(0).mesh.peer_discovered(network.node(1).mesh.device_id(), protocol::Id{9},
                                         network.node(1).address, network.now);
    network.node(0).mesh.peer_discovered(network.node(2).mesh.device_id(), protocol::Id{},
                                         network.node(2).address, network.now);
    network.run_for(milliseconds(5'000));
    EXPECT_TRUE(network.open_links().empty());
}

TEST(Mesh, ReconnectsWithBackoffAfterTheConnectionBreaks) {
    SimulatedMeshNetwork network;
    build(network, 2);
    network.discover_all(kGroup);
    network.run_for(milliseconds(200));
    ASSERT_EQ(network.open_links().size(), 1U);
    const auto [a, b] = *network.open_links().begin();
    network.break_connection(a);
    EXPECT_EQ(network.node(0).observer.disconnects.size(), 1U);
    EXPECT_EQ(network.node(0).observer.disconnects.front().second, Error::NetworkError);
    network.run_for(milliseconds(500)); // the first retry comes after 250 ms
    expect_full_mesh(network);
    EXPECT_EQ(network.node(0).observer.connects, 2U);
}

TEST(Mesh, PeerLossIsDetectedWithinOneSecond) {
    SimulatedMeshNetwork network;
    build(network, 2);
    network.discover_all(kGroup);
    network.run_for(milliseconds(300));
    ASSERT_EQ(network.node(0).observer.connected.size(), 1U);
    network.run_for(milliseconds(2'000)); // idle: heartbeats keep both sides responsive
    ASSERT_TRUE(network.node(0).observer.responsiveness.empty());

    network.node(1).frozen = true; // power loss: no FIN, no answer
    const auto lost_at = network.now;
    while (network.node(0).observer.responsiveness.empty() &&
           network.now < lost_at + milliseconds(2'000)) {
        network.step();
    }
    ASSERT_FALSE(network.node(0).observer.responsiveness.empty());
    EXPECT_FALSE(network.node(0).observer.responsiveness.front().second);
    EXPECT_LE(network.now - lost_at, milliseconds(1'000));

    network.run_for(milliseconds(10'000)); // the session times out after 10 s of silence
    ASSERT_EQ(network.node(0).observer.disconnects.size(), 1U);
    EXPECT_EQ(network.node(0).observer.disconnects.front().second, Error::TimedOut);
}

TEST(Mesh, AShortStallIsNotReportedAsLoss) {
    SimulatedMeshNetwork network;
    build(network, 2);
    network.discover_all(kGroup);
    network.run_for(milliseconds(300));
    network.node(1).frozen = true;
    network.run_for(milliseconds(400)); // shorter than unresponsive_after with one heartbeat lost
    network.node(1).frozen = false;
    network.run_for(milliseconds(1'000));
    EXPECT_TRUE(network.node(0).observer.responsiveness.empty());
}

TEST(Mesh, MotionNeverMovesBackwardsUnderLossAndReordering) {
    SimulatedMeshNetwork network;
    build(network, 2);
    network.discover_all(kGroup);
    network.run_for(milliseconds(300));
    network.datagram_loss = 0.2;
    network.datagram_jitter = Microseconds{4'000};
    const auto receiver = network.node(1).mesh.device_id();
    for (std::int32_t x = 1; x <= 500; ++x) {
        ASSERT_TRUE(network.node(0)
                        .mesh
                        .send_motion(receiver,
                                     protocol::PointerMotion{
                                         .control_id = 7, .x = x, .y = 100, .capture_time_us = 1},
                                     network.now)
                        .has_value());
        network.step();
    }
    network.run_for(milliseconds(50));
    const auto& motions = network.node(1).observer.motions;
    // Lost and overtaken positions are skipped, never applied late.
    ASSERT_GT(motions.size(), 100U);
    EXPECT_LT(motions.size(), 400U);
    for (std::size_t i = 1; i < motions.size(); ++i) {
        EXPECT_GT(motions[i].second.x, motions[i - 1].second.x) << "at " << i;
    }
    EXPECT_GE(motions.back().second.x, 490) << "the cursor still reaches the newest position";
}

TEST(Mesh, ApplicationMessagesFlowBetweenEstablishedPeers) {
    SimulatedMeshNetwork network;
    build(network, 2);
    const auto receiver = network.node(1).mesh.device_id();
    const protocol::DragResult result{.drag_id = 4, .outcome = protocol::DragOutcome::Dropped};
    EXPECT_EQ(network.node(0).mesh.send_message(receiver, result, network.now).error(),
              Error::InvalidState)
        << "no session yet";
    network.discover_all(kGroup);
    network.run_for(milliseconds(300));
    ASSERT_TRUE(network.node(0).mesh.send_message(receiver, result, network.now).has_value());
    EXPECT_EQ(
        network.node(0).mesh.send_message(receiver, protocol::Heartbeat{}, network.now).error(),
        Error::InvalidArgument)
        << "the session's own messages are not for applications";
    network.run_for(milliseconds(10));
    ASSERT_EQ(network.node(1).observer.messages.size(), 1U);
    EXPECT_EQ(network.node(1).observer.messages.front().second, protocol::ControlMessage(result));
}

TEST(Mesh, NothingReadableTravelsAfterTheHandshake) {
    SimulatedMeshNetwork network;
    build(network, 2);
    network.discover_all(kGroup);
    network.run_for(milliseconds(300));
    const auto [low, high] = ordered_pair(network);
    const auto receiver = network.node(high).mesh.device_id();

    const protocol::DragOffer offer{.probe_id = 1,
                                    .drag_id = 2,
                                    .items = {{.kind = protocol::ItemKind::File,
                                               .byte_count = 10,
                                               .content_type = "public.data",
                                               .name = kMarker + ".txt"}}};
    ASSERT_TRUE(network.node(low).mesh.send_message(receiver, offer, network.now).has_value());
    const auto stream = network.node(low).mesh.open_transfer(receiver, network.now);
    ASSERT_TRUE(stream.has_value());
    network.run_for(milliseconds(10));
    const std::vector<std::uint8_t> data(kMarker.begin(), kMarker.end());
    ASSERT_TRUE(
        network.node(low)
            .mesh
            .send_transfer(receiver, stream.value(),
                           protocol::ContentChunk{.request_id = 1, .offset = 0, .data = data},
                           network.now)
            .has_value());
    network.run_for(milliseconds(10));

    // Both arrived intact.
    ASSERT_EQ(network.node(high).observer.messages.size(), 1U);
    EXPECT_EQ(network.node(high).observer.messages.front().second, protocol::ControlMessage(offer));
    ASSERT_EQ(network.node(high).observer.transfer_messages.size(), 1U);
    const auto& chunk = std::get<protocol::ContentChunk>(
        network.node(high).observer.transfer_messages.front().second);
    EXPECT_EQ(chunk.data, data);

    // Neither can be read on the wire.
    for (const auto& bytes : network.wire) {
        EXPECT_FALSE(contains(bytes, kMarker));
    }
}

TEST(Mesh, TransferStreamsCarryMessagesBothWaysWithTheRightIds) {
    SimulatedMeshNetwork network;
    build(network, 2);
    network.discover_all(kGroup);
    network.run_for(milliseconds(300));
    const auto [low, high] = ordered_pair(network); // low dialled: the Noise initiator
    const auto low_id = network.node(low).mesh.device_id();
    const auto high_id = network.node(high).mesh.device_id();

    const auto from_low = network.node(low).mesh.open_transfer(high_id, network.now);
    const auto from_high = network.node(high).mesh.open_transfer(low_id, network.now);
    ASSERT_TRUE(from_low.has_value());
    ASSERT_TRUE(from_high.has_value());
    EXPECT_EQ(from_low.value() % 2, 1U) << "the initiator opens odd IDs";
    EXPECT_EQ(from_high.value() % 2, 0U) << "the responder opens even IDs";
    network.run_for(milliseconds(10));
    EXPECT_EQ(network.node(low).observer.writable, std::vector<std::uint32_t>{from_low.value()});
    ASSERT_EQ(network.node(high).observer.transfers_opened.size(), 1U);
    EXPECT_EQ(network.node(high).observer.transfers_opened.front().second, from_low.value());

    const protocol::ManifestRequest request{
        .request_id = 3,
        .object = {.kind = protocol::ObjectKind::Drag, .object_id = 9, .item_index = 0}};
    ASSERT_TRUE(network.node(low)
                    .mesh.send_transfer(high_id, from_low.value(), request, network.now)
                    .has_value());
    network.run_for(milliseconds(10));
    const protocol::Manifest manifest{
        .request_id = 3,
        .is_last = true,
        .entries = {{.path = "Report.pdf", .kind = protocol::EntryKind::File, .byte_count = 5}}};
    ASSERT_TRUE(network.node(high)
                    .mesh.send_transfer(low_id, from_low.value(), manifest, network.now)
                    .has_value());
    network.run_for(milliseconds(10));
    ASSERT_EQ(network.node(high).observer.transfer_messages.size(), 1U);
    EXPECT_EQ(network.node(high).observer.transfer_messages.front().second,
              protocol::TransferMessage(request));
    ASSERT_EQ(network.node(low).observer.transfer_messages.size(), 1U);
    EXPECT_EQ(network.node(low).observer.transfer_messages.front().second,
              protocol::TransferMessage(manifest));

    // Closing the session ends its streams.
    network.node(low).mesh.disconnect(high_id, protocol::ByeReason::UserDisconnected, network.now);
    network.run_for(milliseconds(10));
    EXPECT_EQ(network.node(low).observer.transfers_closed.size(), 2U);
    EXPECT_EQ(network.node(high).observer.transfers_closed.size(), 2U);
}

TEST(Mesh, AFullTransferQueueNeitherDelaysControlNorGrowsWithoutBound) {
    SimulatedMeshNetwork network;
    build(network, 2);
    network.discover_all(kGroup);
    network.run_for(milliseconds(300));
    network.bandwidth_bytes_per_ms = 10'000; // 10 MB/s on every connection
    const auto [low, high] = ordered_pair(network);
    auto& sender = network.node(low);
    const auto receiver = network.node(high).mesh.device_id();
    const auto stream = sender.mesh.open_transfer(receiver, network.now);
    ASSERT_TRUE(stream.has_value());
    network.run_for(milliseconds(10));
    ASSERT_EQ(sender.observer.writable.size(), 1U);

    // Fill the stream until it refuses: about 4 MiB, 400 ms of sending at this rate.
    const std::vector<std::uint8_t> data(protocol::kMaxChunkData, 0x42);
    std::uint64_t offset = 0;
    for (;;) {
        const auto sent = sender.mesh.send_transfer(
            receiver, stream.value(),
            protocol::ContentChunk{.request_id = 1, .offset = offset, .data = data}, network.now);
        if (!sent) {
            ASSERT_EQ(sent.error(), Error::PeerBusy);
            break;
        }
        offset += data.size();
        ASSERT_LT(offset, std::uint64_t{8} * 1024 * 1024) << "the stream never refused";
    }
    // Framing and tags make the queue reach 4 MiB one chunk earlier.
    EXPECT_GE(offset + data.size(), std::uint64_t{4} * 1024 * 1024);

    // A control message does not wait for the queued megabytes.
    const auto sent_at = network.now;
    ASSERT_TRUE(sender.mesh
                    .send_message(receiver,
                                  protocol::DragResult{.drag_id = 1,
                                                       .outcome = protocol::DragOutcome::Dropped},
                                  network.now)
                    .has_value());
    while (network.node(high).observer.messages.empty()) {
        network.step();
    }
    EXPECT_LE(network.now - sent_at, milliseconds(2));

    // The queue drains; the stream reports itself writable below 1 MiB and accepts more.
    while (sender.observer.writable.size() < 2 && network.now < sent_at + milliseconds(1'000)) {
        network.step();
    }
    ASSERT_EQ(sender.observer.writable.size(), 2U);
    EXPECT_GE(network.now - sent_at, milliseconds(250)) << "about 3 MiB had to go first";
    EXPECT_TRUE(
        sender.mesh
            .send_transfer(receiver, stream.value(),
                           protocol::ContentChunk{.request_id = 1, .offset = offset, .data = data},
                           network.now)
            .has_value());
    network.run_for(milliseconds(200));
    EXPECT_EQ(network.node(high).observer.transfer_messages.size(), offset / data.size() + 1)
        << "every chunk arrived";
}

TEST(Mesh, APausedStreamHoldsItsSenderBackAndResumesInOrder) {
    SimulatedMeshNetwork network;
    build(network, 2);
    network.discover_all(kGroup);
    network.run_for(milliseconds(300));
    network.bandwidth_bytes_per_ms = 10'000;
    const auto [low, high] = ordered_pair(network);
    auto& sender = network.node(high);
    auto& receiver = network.node(low);
    const auto stream = receiver.mesh.open_transfer(sender.mesh.device_id(), network.now);
    ASSERT_TRUE(stream.has_value());
    network.run_for(milliseconds(10));
    ASSERT_EQ(sender.observer.transfers_opened.size(), 1U);

    receiver.mesh.pause_transfer(sender.mesh.device_id(), stream.value(), true, network.now);
    const std::vector<std::uint8_t> data(protocol::kMaxChunkData, 0x42);
    std::uint64_t sent = 0;
    while (sender.mesh
               .send_transfer(receiver.mesh.device_id(), stream.value(),
                              protocol::ContentChunk{.request_id = 1, .offset = sent, .data = data},
                              network.now)
               .has_value()) {
        sent += data.size();
        ASSERT_LT(sent, std::uint64_t{8} * 1024 * 1024) << "the stream never refused";
    }
    network.run_for(milliseconds(1'000));
    EXPECT_TRUE(receiver.observer.transfer_messages.empty()) << "nothing read while paused";
    EXPECT_EQ(sender.observer.writable.size(), 0U) << "the sender stays held back";

    receiver.mesh.pause_transfer(sender.mesh.device_id(), stream.value(), false, network.now);
    network.run_for(milliseconds(1'000));
    ASSERT_EQ(receiver.observer.transfer_messages.size(), sent / data.size());
    std::uint64_t expected = 0;
    for (const auto& [id, message] : receiver.observer.transfer_messages) {
        EXPECT_EQ(std::get<protocol::ContentChunk>(message).offset, expected);
        expected += data.size();
    }
    EXPECT_EQ(sender.observer.writable.size(), 1U);
}

TEST(Mesh, ForgedAndReplayedTransferHellosAreRefused) {
    SimulatedMeshNetwork network;
    build(network, 2);
    network.discover_all(kGroup);
    network.run_for(milliseconds(300));
    const auto [low, high] = ordered_pair(network);
    const auto high_id = network.node(high).mesh.device_id();

    // A legitimate stream, to capture its TransferHello from the wire.
    const std::size_t wire_before = network.wire.size();
    const auto stream = network.node(low).mesh.open_transfer(high_id, network.now);
    ASSERT_TRUE(stream.has_value());
    network.run_for(milliseconds(10));
    std::vector<std::uint8_t> hello;
    for (std::size_t i = wire_before; i < network.wire.size(); ++i) {
        if (network.wire[i].size() == 4 + 24 && network.wire[i][4 + 3] == 0x81) {
            hello = network.wire[i];
        }
    }
    ASSERT_FALSE(hello.empty());

    const std::size_t replay = network.raw_connect(high);
    network.raw_send(replay, hello);
    network.run_for(milliseconds(10));
    EXPECT_FALSE(network.raw_open(replay)) << "a stream ID is never used twice";

    auto forged = hello;
    forged[4 + 4] ^= 1U; // a token nobody issued
    const std::size_t stranger = network.raw_connect(high);
    network.raw_send(stranger, forged);
    network.run_for(milliseconds(10));
    EXPECT_FALSE(network.raw_open(stranger));

    auto wrong_side = hello;
    wrong_side[4 + 4 + 16] =
        static_cast<std::uint8_t>(stream.value() + 1); // the responder's parity
    const std::size_t impostor = network.raw_connect(high);
    network.raw_send(impostor, wrong_side);
    network.run_for(milliseconds(10));
    EXPECT_FALSE(network.raw_open(impostor));

    EXPECT_EQ(network.node(high).observer.transfers_opened.size(), 1U);
}

TEST(Mesh, ConnectionsThatSendNothingAreClosed) {
    SimulatedMeshNetwork network;
    build(network, 1);
    const std::size_t silent = network.raw_connect(0);
    network.run_for(milliseconds(9'000));
    EXPECT_TRUE(network.raw_open(silent));
    network.run_for(milliseconds(2'000));
    EXPECT_FALSE(network.raw_open(silent));
}

TEST(Mesh, PairingShowsTheSameCodeAndEstablishesTheSession) {
    SimulatedMeshNetwork network;
    network.add(SimulatedMeshNetwork::config(kGroup, "Mac"));
    network.add(SimulatedMeshNetwork::config(protocol::Id{}, "New PC"));
    network.node(0).observer.trust = osdrop::mesh::TrustDecision::Unknown;
    network.node(1).observer.trust = osdrop::mesh::TrustDecision::Unknown;
    const auto connection = network.node(1).mesh.pair_with(network.node(0).address, network.now);
    network.run_for(milliseconds(50));
    ASSERT_EQ(network.node(0).observer.pairing_codes.size(), 1U);
    ASSERT_EQ(network.node(1).observer.pairing_codes.size(), 1U);
    EXPECT_EQ(network.node(0).observer.pairing_codes.front().second,
              network.node(1).observer.pairing_codes.front().second);
    EXPECT_TRUE(network.node(0).observer.connected.empty());

    ASSERT_TRUE(network.node(1).mesh.confirm_pairing(connection, true, network.now).has_value());
    ASSERT_TRUE(network.node(0)
                    .mesh
                    .confirm_pairing(network.node(0).observer.pairing_codes.front().first, true,
                                     network.now)
                    .has_value());
    network.run_for(milliseconds(50));
    EXPECT_EQ(network.node(0).observer.paired.size(), 1U);
    EXPECT_EQ(network.node(1).observer.paired.size(), 1U);
    EXPECT_EQ(network.node(0).observer.connected.size(), 1U);
    EXPECT_EQ(network.node(1).observer.connected.size(), 1U);
}

TEST(Mesh, ARejectedPairingEndsOnBothSides) {
    SimulatedMeshNetwork network;
    network.add(SimulatedMeshNetwork::config(kGroup, "Mac"));
    network.add(SimulatedMeshNetwork::config(protocol::Id{}, "New PC"));
    network.node(0).observer.trust = osdrop::mesh::TrustDecision::Unknown;
    network.node(1).observer.trust = osdrop::mesh::TrustDecision::Unknown;
    const auto connection = network.node(1).mesh.pair_with(network.node(0).address, network.now);
    network.run_for(milliseconds(50));
    ASSERT_EQ(network.node(0).observer.pairing_codes.size(), 1U);
    ASSERT_TRUE(network.node(0)
                    .mesh
                    .confirm_pairing(network.node(0).observer.pairing_codes.front().first, false,
                                     network.now)
                    .has_value());
    network.run_for(milliseconds(50));
    ASSERT_EQ(network.node(1).observer.pairing_failures.size(), 1U);
    EXPECT_EQ(network.node(1).observer.pairing_failures.front(),
              std::pair(connection, Error::PairingRejected));
    EXPECT_EQ(network.node(0).observer.pairing_failures.size(), 1U);
    EXPECT_TRUE(network.open_links().empty());
}

TEST(Mesh, AComputerAtADiscoveredAddressMustHaveTheAdvertisedId) {
    SimulatedMeshNetwork network;
    build(network, 3);
    // Node 0 believes node 1 lives at node 2's address (a stale or forged advertisement).
    network.node(0).mesh.peer_discovered(network.node(1).mesh.device_id(), kGroup,
                                         network.node(2).address, network.now);
    network.run_for(milliseconds(5'000));
    EXPECT_TRUE(network.node(0).observer.connected.empty());
    EXPECT_TRUE(network.open_links().empty());
}

TEST(Mesh, SleepingPeersAreReportedAsSuch) {
    SimulatedMeshNetwork network;
    build(network, 2);
    network.discover_all(kGroup);
    network.run_for(milliseconds(300));
    network.node(1).mesh.close_all(protocol::ByeReason::Sleeping, network.now);
    network.node(1).frozen = true; // asleep
    network.run_for(milliseconds(10));
    ASSERT_EQ(network.node(0).observer.disconnects.size(), 1U);
    EXPECT_EQ(network.node(0).observer.disconnects.front().second, Error::PeerSleeping);
    EXPECT_EQ(network.node(1).observer.disconnects.size(), 1U);
}

} // namespace
