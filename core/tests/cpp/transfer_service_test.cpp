// The transfer engine between group members over the simulated network, with real files.

#include "group_simulation.hpp"
#include "mesh_simulation.hpp"
#include "temp_folder.hpp"
#include "transfer/files.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace protocol = osdrop::protocol;
namespace transfer = osdrop::transfer;
using osdrop::Error;
using osdrop::test::form_group;
using osdrop::test::listing;
using osdrop::test::pattern;
using osdrop::test::read_file;
using osdrop::test::SimulatedMeshNetwork;
using osdrop::test::SimulatedNode;
using osdrop::test::TempFolder;
using osdrop::test::write_file;
using std::chrono::milliseconds;

constexpr protocol::ObjectRef kItem{
    .kind = protocol::ObjectKind::Drag, .object_id = 7, .item_index = 0};
constexpr std::int64_t kTime = 1'600'000'000'000'000; // 2020-09-13

bool can_link(const TempFolder& folder) {
    std::error_code error;
    fs::create_symlink("x", folder / ".probe", error);
    fs::remove(folder / ".probe", error);
    return !error;
}

class TransferService : public ::testing::Test {
protected:
    void form(std::size_t count = 2) {
        network.capture_wire = false;
        form_group(network, count);
        for (std::size_t i = 0; i < count; ++i) {
            ASSERT_EQ(network.node(i).mesh.connected_peers().size(), count - 1);
        }
    }

    SimulatedNode& sender() { return network.node(0); }
    SimulatedNode& receiver() { return network.node(1); }

    /// Offers `roots` from the sender to everyone and waits for the scan.
    void offer(std::vector<fs::path> roots, const protocol::ObjectRef& item = kItem) {
        std::set<osdrop::mesh::DeviceId> audience;
        for (std::size_t i = 0; i < network.size(); ++i) {
            audience.insert(network.node(i).mesh.device_id());
        }
        const std::size_t before = sender().transfer_observer.offers.size();
        ASSERT_TRUE(sender().transfers->offer(item, std::move(roots), audience).has_value());
        network.step();
        ASSERT_EQ(sender().transfer_observer.offers.size(), before + 1);
    }

    osdrop::Expected<transfer::TransferId, Error> receive(const protocol::ObjectRef& item = kItem) {
        auto id =
            receiver().transfers->receive(sender().mesh.device_id(), item, destination.path());
        receiver().mesh.flush(network.now);
        return id;
    }

    /// Steps until the receiver reported `count` ends, at most `limit` of simulated time.
    void run_until_finished(std::size_t count = 1, milliseconds limit = milliseconds(60'000)) {
        const auto end = network.now + limit;
        while (receiver().transfer_observer.finished.size() < count && network.now < end) {
            network.step();
        }
        ASSERT_EQ(receiver().transfer_observer.finished.size(), count) << "no end reported";
    }

    const transfer::TransferOutcome& outcome(std::size_t index = 0) {
        return receiver().transfer_observer.finished.at(index);
    }

    SimulatedMeshNetwork network;
    TempFolder source;
    TempFolder destination;
};

TEST_F(TransferService, AFolderTreeArrivesWithContentsTimesFlagsAndInnerLinks) {
    form();
    write_file(source / "Project/empty.txt", "");
    write_file(source / "Project/one-chunk.bin", pattern(protocol::kMaxChunkData));
    write_file(source / "Project/chunk-and-a-byte.bin", pattern(protocol::kMaxChunkData + 1, 3));
    write_file(source / "Project/docs/big.bin", pattern(3'000'000, 9));
    write_file(source / "Project/docs/Cafe\xCC\x81 menu.txt", "decomposed name");
    write_file(source / "Project/tool.sh", "#!/bin/sh\n");
    fs::create_directories(source / "Project/empty-folder");
    fs::permissions(source / "Project/tool.sh", fs::perms::owner_exec, fs::perm_options::add);
    transfer::set_modified_us(source / "Project/docs/big.bin", kTime);
    const bool links = can_link(source);
    if (links) {
        fs::create_symlink("../one-chunk.bin", source / "Project/docs/inside");
        fs::create_symlink("../../outside.txt", source / "Project/docs/outside");
    }
    transfer::set_modified_us(source / "Project/docs", kTime);
    offer({source / "Project"});
    const auto& ready = sender().transfer_observer.offers.back();
    EXPECT_FALSE(ready.error.has_value());
    EXPECT_EQ(ready.total_bytes, 3'000'000U + (2 * protocol::kMaxChunkData) + 1 + 15 + 10);

    const auto id = receive();
    ASSERT_TRUE(id.has_value());
    run_until_finished();

    EXPECT_EQ(outcome().id, id.value());
    ASSERT_FALSE(outcome().error.has_value()) << static_cast<int>(*outcome().error);
    EXPECT_EQ(outcome().items, std::vector<fs::path>{destination / "Project"});
    EXPECT_EQ(outcome().skipped_links, links ? 1U : 0U);
    std::vector<std::string> expected{"Project",
                                      "Project/chunk-and-a-byte.bin",
                                      "Project/docs",
                                      "Project/docs/Caf\xC3\xA9 menu.txt",
                                      "Project/docs/big.bin",
                                      "Project/empty-folder",
                                      "Project/empty.txt",
                                      "Project/one-chunk.bin",
                                      "Project/tool.sh"};
    if (links) {
        expected.emplace_back("Project/docs/inside");
    }
    std::ranges::sort(expected);
    EXPECT_EQ(listing(destination.path()), expected);
    for (const char* file : {"Project/one-chunk.bin", "Project/chunk-and-a-byte.bin",
                             "Project/docs/big.bin", "Project/tool.sh", "Project/empty.txt"}) {
        EXPECT_EQ(read_file(destination / file), read_file(source / file)) << file;
    }
    EXPECT_EQ(transfer::modified_us(destination / "Project/docs/big.bin").value_or(0) / 1000,
              kTime / 1000);
    EXPECT_EQ(transfer::modified_us(destination / "Project/docs").value_or(0) / 1000, kTime / 1000);
#ifndef _WIN32
    EXPECT_TRUE(transfer::is_executable(destination / "Project/tool.sh"));
    EXPECT_FALSE(transfer::is_executable(destination / "Project/docs/big.bin"));
#endif
    if (links) {
        EXPECT_EQ(fs::read_symlink(destination / "Project/docs/inside"), "../one-chunk.bin");
    }
    const auto& last = receiver().transfer_observer.progress.back();
    EXPECT_EQ(last.bytes_done, last.bytes_total);
    EXPECT_EQ(last.files_done, 6U);
    EXPECT_EQ(last.files_total, 6U);
}

TEST_F(TransferService, NamesTakenInTheDestinationAreNumbered) {
    form();
    write_file(source / "Project/a.txt", "new");
    write_file(source / "report.pdf", "new report");
    write_file(destination / "Project/old.txt", "keep me");
    write_file(destination / "report.pdf", "old report");
    offer({source / "Project"});
    offer({source / "report.pdf"},
          {.kind = protocol::ObjectKind::Drag, .object_id = 7, .item_index = 1});
    ASSERT_TRUE(receive().has_value());
    ASSERT_TRUE(
        receive({.kind = protocol::ObjectKind::Drag, .object_id = 7, .item_index = 1}).has_value());
    run_until_finished(2);

    EXPECT_EQ(outcome(0).items, std::vector<fs::path>{destination / "Project (2)"});
    EXPECT_EQ(outcome(1).items, std::vector<fs::path>{destination / "report (2).pdf"});
    EXPECT_EQ(listing(destination.path()),
              (std::vector<std::string>{"Project", "Project (2)", "Project (2)/a.txt",
                                        "Project/old.txt", "report (2).pdf", "report.pdf"}));
    const std::string old_report = "old report";
    EXPECT_EQ(read_file(destination / "report.pdf"),
              std::vector<std::uint8_t>(old_report.begin(), old_report.end()));
}

TEST_F(TransferService, ContentThatDoesNotMatchItsDigestIsRemoved) {
    form();
    write_file(source / "Project/a.bin", pattern(200'000));
    write_file(source / "Project/b.bin", pattern(300'000, 1));
    offer({source / "Project"});
    ASSERT_TRUE(receive().has_value());
    run_until_finished();
    ASSERT_FALSE(outcome().error.has_value());

    // The file changes but keeps its size and time: the sender still has the old digest.
    write_file(source / "Project/b.bin", pattern(300'000, 2));
    transfer::set_modified_us(source / "Project/b.bin",
                              transfer::modified_us(destination / "Project/b.bin").value());
    fs::remove_all(destination / "Project");
    ASSERT_TRUE(receive().has_value());
    run_until_finished(2);
    ASSERT_TRUE(outcome(1).error.has_value());
    EXPECT_EQ(*outcome(1).error, Error::IntegrityFailed);
    EXPECT_TRUE(listing(destination.path()).empty()) << "nothing is left behind";
}

TEST_F(TransferService, TamperingOnTheWayEndsTheTransferAndLeavesNothing) {
    form();
    write_file(source / "Project/a.bin", pattern(2'000'000));
    offer({source / "Project"});
    int seen = 0;
    network.tamper = [&](std::vector<std::uint8_t>& bytes) {
        if (bytes.size() > 60'000 && ++seen == 10) {
            bytes[bytes.size() / 2] ^= 0x01U;
        }
    };
    ASSERT_TRUE(receive().has_value());
    run_until_finished();
    ASSERT_TRUE(outcome().error.has_value());
    EXPECT_EQ(*outcome().error, Error::DecryptionFailed);
    EXPECT_TRUE(listing(destination.path()).empty());
}

TEST_F(TransferService, CancellingFromEitherSideLeavesNothingBehind) {
    form();
    network.bandwidth_bytes_per_ms = 20'000; // 20 MB/s
    write_file(source / "Project/first.bin", pattern(500'000));
    write_file(source / "Project/second.bin", pattern(20'000'000, 1));
    offer({source / "Project"});

    const auto id = receive();
    ASSERT_TRUE(id.has_value());
    while (receiver().transfer_observer.progress.empty() ||
           receiver().transfer_observer.progress.back().bytes_done < 2'000'000) {
        network.step();
        ASSERT_TRUE(receiver().transfer_observer.finished.empty());
    }
    EXPECT_FALSE(listing(destination.path()).empty()) << "it was writing";
    receiver().transfers->cancel(id.value());
    receiver().mesh.flush(network.now);
    run_until_finished();
    EXPECT_EQ(outcome().error, Error::Cancelled);
    EXPECT_TRUE(listing(destination.path()).empty());

    // The sender withdraws the item while it is being fetched.
    const auto second = receive();
    ASSERT_TRUE(second.has_value());
    while (receiver().transfer_observer.progress.back().id != second.value() ||
           receiver().transfer_observer.progress.back().bytes_done < 2'000'000) {
        network.step();
    }
    sender().transfers->withdraw(protocol::ObjectKind::Drag, kItem.object_id);
    sender().mesh.flush(network.now);
    run_until_finished(2);
    EXPECT_EQ(outcome(1).error, Error::Cancelled);
    EXPECT_TRUE(listing(destination.path()).empty());
}

TEST_F(TransferService, TheSenderReportsItsProgressUntilEveryByteWentOut) {
    form();
    network.bandwidth_bytes_per_ms = 20'000; // 20 MB/s
    write_file(source / "Project/a.bin", pattern(3'000'000));
    write_file(source / "Project/b.txt", "hello");
    offer({source / "Project"});
    ASSERT_TRUE(receive().has_value());
    run_until_finished();
    ASSERT_FALSE(outcome().error);
    const auto& sends = sender().transfer_observer.sends;
    ASSERT_GE(sends.size(), 2U) << "progress along the way";
    for (std::size_t i = 0; i + 1 < sends.size(); ++i) {
        EXPECT_EQ(sends[i].state, transfer::SendState::Sending);
        EXPECT_LE(sends[i].bytes_sent, sends[i + 1].bytes_sent);
    }
    EXPECT_EQ(sends.back(), (transfer::SendProgress{.device = receiver().mesh.device_id(),
                                                    .item = kItem,
                                                    .bytes_sent = 3'000'005,
                                                    .bytes_total = 3'000'005,
                                                    .state = transfer::SendState::Done}));
}

TEST_F(TransferService, AWithdrawnItemStopsItsSending) {
    form();
    network.bandwidth_bytes_per_ms = 20'000;
    write_file(source / "big.bin", pattern(20'000'000));
    offer({source / "big.bin"});
    ASSERT_TRUE(receive().has_value());
    while (sender().transfer_observer.sends.empty()) {
        network.step();
    }
    sender().transfers->withdraw(protocol::ObjectKind::Drag, kItem.object_id);
    sender().mesh.flush(network.now);
    run_until_finished();
    const auto& sends = sender().transfer_observer.sends;
    EXPECT_EQ(sends.back().state, transfer::SendState::Stopped);
    EXPECT_EQ(sends.back().bytes_total, 20'000'000U);
    EXPECT_LT(sends.back().bytes_sent, 20'000'000U);
    EXPECT_EQ(
        std::ranges::count(sends, transfer::SendState::Stopped, &transfer::SendProgress::state), 1);
}

TEST_F(TransferService, NothingIsWrittenWithoutRoomForIt) {
    network.transfer_tunables.space_margin = std::uint64_t{1} << 62U; // more than any disk
    form();
    write_file(source / "Project/a.bin", pattern(1000));
    offer({source / "Project"});
    ASSERT_TRUE(receive().has_value());
    run_until_finished();
    EXPECT_EQ(outcome().error, Error::NoSpace);
    EXPECT_TRUE(listing(destination.path()).empty());
}

TEST_F(TransferService, TheFilesPermissionAndTheAudienceAreEnforced) {
    form();
    write_file(source / "Project/a.bin", pattern(4'000'000));
    offer({source / "Project"});

    // Offered to someone else only.
    const protocol::ObjectRef hidden{
        .kind = protocol::ObjectKind::Drag, .object_id = 8, .item_index = 0};
    ASSERT_TRUE(sender()
                    .transfers->offer(hidden, {source / "Project"}, {sender().mesh.device_id()})
                    .has_value());
    network.step();
    ASSERT_TRUE(receive(hidden).has_value());
    run_until_finished();
    EXPECT_EQ(outcome().error, Error::NotPermitted);

    // The receiver takes the sender's files permission away in the middle of a transfer.
    network.bandwidth_bytes_per_ms = 10'000;
    ASSERT_TRUE(receive().has_value());
    while (receiver().transfer_observer.progress.empty() ||
           receiver().transfer_observer.progress.back().bytes_done == 0) {
        network.step();
    }
    ASSERT_TRUE(receiver()
                    .group
                    ->set_permissions(sender().mesh.device_id(),
                                      {.control = true, .files = false, .clipboard = true},
                                      network.now)
                    .has_value());
    receiver().mesh.flush(network.now);
    run_until_finished(2);
    EXPECT_EQ(outcome(1).error, Error::NotPermitted);
    EXPECT_TRUE(listing(destination.path()).empty());
    EXPECT_EQ(receive().error(), Error::NotPermitted);
}

TEST_F(TransferService, ASlowDiskPausesTheStreamInsteadOfFillingMemory) {
    network.transfer_tunables.write_high_water = std::size_t{1} * 1024 * 1024;
    network.transfer_tunables.write_low_water = std::size_t{256} * 1024;
    form();
    write_file(source / "big.bin", pattern(8'000'000));
    offer({source / "big.bin"});
    receiver().executor.hold_files = true;
    ASSERT_TRUE(receive().has_value());
    // Manifest and planning wait for the disk too: let them through, then hold the writes.
    receiver().executor.hold_files = false;
    network.step();
    receiver().executor.hold_files = true;
    network.run_for(milliseconds(2'000));
    const std::size_t queued = receiver().executor.queued_files();
    EXPECT_GT(queued, 0U);
    // Writes waiting: at most the high-water mark plus what was in flight when it paused.
    EXPECT_LE(queued * protocol::kMaxChunkData,
              network.transfer_tunables.write_high_water + std::size_t{1} * 1024 * 1024);
    receiver().executor.hold_files = false;
    run_until_finished();
    ASSERT_FALSE(outcome().error.has_value());
    EXPECT_EQ(read_file(destination / "big.bin"), read_file(source / "big.bin"));
}

TEST_F(TransferService, StreamsPerPeerAreLimitedAndTheRestWait) {
    network.transfer_tunables.max_streams = 2;
    form();
    network.bandwidth_bytes_per_ms = 5'000;
    for (std::uint32_t i = 0; i < 5; ++i) {
        write_file(source / ("file" + std::to_string(i) + ".bin"),
                   pattern(400'000, static_cast<std::uint8_t>(i)));
        offer({source / ("file" + std::to_string(i) + ".bin")},
              {.kind = protocol::ObjectKind::Drag, .object_id = 7, .item_index = i});
    }
    for (std::uint32_t i = 0; i < 5; ++i) {
        ASSERT_TRUE(receive({.kind = protocol::ObjectKind::Drag, .object_id = 7, .item_index = i})
                        .has_value());
    }
    std::size_t most_links = 0;
    const auto end = network.now + milliseconds(60'000);
    while (receiver().transfer_observer.finished.size() < 5 && network.now < end) {
        network.step();
        most_links = std::max(most_links, network.open_links().size());
    }
    ASSERT_EQ(receiver().transfer_observer.finished.size(), 5U);
    EXPECT_EQ(most_links, 3U) << "one session and two streams";
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_FALSE(outcome(i).error.has_value()) << i;
    }
    EXPECT_EQ(listing(destination.path()).size(), 5U);
}

TEST_F(TransferService, ManySmallFilesArriveAndProgressCountsThem) {
    form();
    for (int i = 0; i < 300; ++i) {
        write_file(source / ("Many/f" + std::to_string(i)), pattern(static_cast<std::size_t>(i)));
    }
    offer({source / "Many"});
    ASSERT_TRUE(receive().has_value());
    run_until_finished();
    ASSERT_FALSE(outcome().error.has_value());
    EXPECT_EQ(listing(destination.path()).size(), 301U);
    EXPECT_EQ(read_file(destination / "Many/f299"), pattern(299));
    EXPECT_EQ(receiver().transfer_observer.progress.back().files_done, 300U);
}

TEST_F(TransferService, ASourceThatChangesDuringTheTransferFails) {
    form();
    network.bandwidth_bytes_per_ms = 10'000;
    write_file(source / "Project/a.bin", pattern(12'000'000)); // read ahead covers ~5 MB
    offer({source / "Project"});
    ASSERT_TRUE(receive().has_value());
    while (receiver().transfer_observer.progress.empty() ||
           receiver().transfer_observer.progress.back().bytes_done == 0) {
        network.step();
    }
    transfer::set_modified_us(source / "Project/a.bin", kTime); // someone saved it
    run_until_finished();
    EXPECT_EQ(outcome().error, Error::IoError);
    EXPECT_TRUE(listing(destination.path()).empty());
}

TEST_F(TransferService, ALostPeerEndsItsTransfersAndLeavesNothing) {
    form();
    network.bandwidth_bytes_per_ms = 10'000;
    write_file(source / "Project/a.bin", pattern(5'000'000));
    offer({source / "Project"});
    ASSERT_TRUE(receive().has_value());
    while (receiver().transfer_observer.progress.empty() ||
           receiver().transfer_observer.progress.back().bytes_done == 0) {
        network.step();
    }
    sender().mesh.disconnect(receiver().mesh.device_id(), protocol::ByeReason::UserDisconnected,
                             network.now);
    run_until_finished();
    EXPECT_TRUE(outcome().error.has_value());
    EXPECT_TRUE(listing(destination.path()).empty());
}

} // namespace
