// A drag that came from another computer (DEVPLAN Phase 14): its items are received at once, the
// tree is known once core planned every item, and a file is read while core writes it, its last
// byte only once core verified it.
#include <osdrop/win/network_drag.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace drag = osdrop::win::drag;
namespace fs = std::filesystem;
using std::chrono::milliseconds;

constexpr drag::SourceId kSource{1, 2, 3};
constexpr std::uint32_t kDragId = 9;
constexpr std::uint8_t kFile = 1;
constexpr std::uint8_t kFolder = 2;
constexpr std::uint8_t kLink = 3;

/// A folder of its own under the system's temporary folder, removed afterwards.
class TempFolder {
public:
    TempFolder() {
        std::random_device random;
        path_ = fs::temp_directory_path() / ("osdrop-network-drag-" + std::to_string(random()));
        fs::create_directories(path_);
    }
    ~TempFolder() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
    TempFolder(TempFolder&&) = delete;
    TempFolder& operator=(TempFolder&&) = delete;
    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

/// Reads a file through its own stream, as POSIX lets a file be renamed while open.
class StreamFile final : public drag::GrowingFile {
public:
    explicit StreamFile(const fs::path& path) : in_(path, std::ios::binary) {}
    [[nodiscard]] bool open() const { return static_cast<bool>(in_); }
    std::optional<std::size_t> read(std::uint64_t offset, std::span<std::uint8_t> buffer) override {
        in_.clear();
        in_.seekg(static_cast<std::streamoff>(offset));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): bytes as the stream's chars
        in_.read(reinterpret_cast<char*>(buffer.data()),
                 static_cast<std::streamsize>(buffer.size()));
        return static_cast<std::size_t>(in_.gcount());
    }

private:
    std::ifstream in_;
};

drag::FileOpener stream_opener() {
    return [](const fs::path& path) -> std::unique_ptr<drag::GrowingFile> {
        auto file = std::make_unique<StreamFile>(path);
        return file->open() ? std::move(file) : nullptr;
    };
}

class FakeTransfers final : public drag::DragTransfers {
public:
    struct Received {
        std::uint32_t index = 0;
        fs::path folder;
    };
    std::vector<Received> received;
    std::vector<std::uint64_t> cancelled;
    std::optional<std::uint32_t> refuse_index;
    drag::TransferRouter router;

    std::optional<std::uint64_t> receive(const drag::SourceId& source, std::uint32_t drag_id,
                                         std::uint32_t index, const fs::path& folder,
                                         const std::weak_ptr<drag::TransferSink>& sink) override {
        EXPECT_EQ(source, kSource);
        EXPECT_EQ(drag_id, kDragId);
        router.starting();
        const std::optional<std::uint64_t> id =
            refuse_index == index ? std::nullopt : std::optional<std::uint64_t>(100 + index);
        received.push_back({.index = index, .folder = folder});
        router.started(id, sink);
        return id;
    }
    void cancel(std::uint64_t transfer_id) override { cancelled.push_back(transfer_id); }
};

class NetworkDragTest : public ::testing::Test {
protected:
    std::shared_ptr<drag::NetworkDrag> make(std::vector<drag::ArrivingItem> items) {
        return std::make_shared<drag::NetworkDrag>(transfers, kSource, kDragId, std::move(items),
                                                   staging.path(), stream_opener());
    }

    FakeTransfers transfers;
    TempFolder staging;
};

class RecordingSink final : public drag::TransferSink {
public:
    std::vector<std::string> events;
    void planned(std::uint64_t id, const std::vector<drag::PlannedEntry>& entries) override {
        events.push_back("planned " + std::to_string(id) + " " + std::to_string(entries.size()));
    }
    void written(std::uint64_t id, std::uint32_t entry, const fs::path& /*path*/,
                 std::uint64_t bytes, bool complete) override {
        events.push_back("written " + std::to_string(id) + " " + std::to_string(entry) + " " +
                         std::to_string(bytes) + (complete ? " complete" : ""));
    }
    void finished(std::uint64_t id, osdrop_result result,
                  const std::vector<fs::path>& /*items*/) override {
        events.push_back("finished " + std::to_string(id) + " " +
                         std::to_string(static_cast<int>(result)));
    }
};

TEST(TransferRouter, EventsBeforeTheIdAreKeptAndDeliveredFirst) {
    drag::TransferRouter router;
    const auto sink = std::make_shared<RecordingSink>();
    router.planned(4, {}); // nobody receives: dropped
    router.starting();
    router.planned(5, {{.path = "a"}});
    router.written(5, 0, "x", 3, false);
    router.written(6, 0, "y", 1, false); // another transfer's
    router.started(5, sink);
    router.written(5, 0, "x", 4, true);
    router.finished(5, OSDROP_OK, {});
    router.written(5, 0, "x", 4, true); // after the end: nobody
    router.starting();
    router.started(6, sink); // its event was dropped once no receive ran
    router.written(6, 0, "y", 2, false);
    EXPECT_EQ(sink->events,
              (std::vector<std::string>{"planned 5 1", "written 5 0 3", "written 5 0 4 complete",
                                        "finished 5 0", "written 6 0 2"}));
}

TEST_F(NetworkDragTest, EveryItemIsReceivedAtOnceIntoAFolderOfItsOwn) {
    const auto network = make({{.name = "a.txt", .folder = false, .size = 3},
                               {.name = "Photos", .folder = true, .size = 10}});
    ASSERT_TRUE(network->start());
    ASSERT_EQ(transfers.received.size(), 2U);
    EXPECT_EQ(transfers.received[0].folder, staging.path() / "0");
    EXPECT_EQ(transfers.received[1].folder, staging.path() / "1");
    EXPECT_TRUE(fs::is_directory(staging.path() / "1"));
    EXPECT_EQ(network->total_bytes(), 13U);
}

TEST_F(NetworkDragTest, ATransferThatCannotStartCancelsTheOthers) {
    transfers.refuse_index = 1;
    const auto network = make({{.name = "a"}, {.name = "b"}});
    EXPECT_FALSE(network->start());
    EXPECT_EQ(transfers.cancelled, std::vector<std::uint64_t>{100});
    EXPECT_FALSE(network->entries(milliseconds(0)).has_value());
}

TEST_F(NetworkDragTest, TheTreeIsKnownOnceEveryItemIsPlannedWithoutLinks) {
    const auto network = make({{.name = "Photos", .folder = true}, {.name = "b.txt"}});
    ASSERT_TRUE(network->start());
    transfers.router.planned(
        100, {{.path = "Photos", .kind = kFolder},
              {.path = "Photos/sea.jpg", .kind = kFile, .size = 7, .modified_us = 1},
              {.path = "Photos/latest", .kind = kLink}});
    EXPECT_FALSE(network->entries(milliseconds(0)).has_value()) << "b.txt is not planned yet";
    transfers.router.planned(101, {{.path = "b.txt", .kind = kFile, .size = 2}});
    const auto tree = network->entries(milliseconds(0));
    ASSERT_TRUE(tree.has_value());
    ASSERT_EQ(tree->size(), 3U);
    EXPECT_EQ((*tree)[0].path, "Photos");
    EXPECT_TRUE((*tree)[0].folder);
    EXPECT_EQ((*tree)[1].path, "Photos/sea.jpg");
    EXPECT_EQ((*tree)[1].size, 7U);
    EXPECT_EQ((*tree)[1].write_time, 116'444'736'000'000'010U) << "FILETIME of 1970 + 1 us";
    EXPECT_EQ((*tree)[2].path, "b.txt");
}

TEST_F(NetworkDragTest, ReadingWaitsForTheCheckThenFollowsTheFileToItsPlace) {
    const auto network = make({{.name = "a.bin", .size = 10}});
    ASSERT_TRUE(network->start());
    transfers.router.planned(100, {{.path = "a.bin", .kind = kFile, .size = 10}});
    ASSERT_TRUE(network->entries(milliseconds(0)).has_value());
    const fs::path temporary = staging.path() / "0" / ".osdrop-1-0.part";
    write(temporary, "0123456789");
    transfers.router.written(100, 0, temporary, 10, false);

    const auto reader = network->open(0);
    ASSERT_NE(reader, nullptr);
    std::vector<std::uint8_t> buffer(10);
    EXPECT_EQ(reader->read(buffer), 9U) << "every byte but the last";
    auto last = std::async(std::launch::async, [&] { return reader->read(buffer); });
    EXPECT_EQ(last.wait_for(milliseconds(50)), std::future_status::timeout);
    const fs::path placed = staging.path() / "0" / "a.bin";
    fs::rename(temporary, placed);
    transfers.router.written(100, 0, placed, 10, true);
    ASSERT_EQ(last.wait_for(milliseconds(2000)), std::future_status::ready);
    EXPECT_EQ(last.get(), 1U);
    EXPECT_EQ(buffer[0], '9');
    EXPECT_EQ(reader->read(buffer), 0U) << "the end";
}

TEST_F(NetworkDragTest, AFailedTransferBreaksTheReadInsteadOfEndingIt) {
    const auto network = make({{.name = "a.bin", .size = 10}});
    ASSERT_TRUE(network->start());
    transfers.router.planned(100, {{.path = "a.bin", .kind = kFile, .size = 10}});
    ASSERT_TRUE(network->entries(milliseconds(0)).has_value());
    const fs::path temporary = staging.path() / "0" / ".osdrop-1-0.part";
    write(temporary, "0123");
    transfers.router.written(100, 0, temporary, 4, false);

    drag::FileStream stream(*network, 0, 10);
    std::vector<std::uint8_t> buffer(10);
    auto read = std::async(std::launch::async, [&] { return stream.read(buffer); });
    EXPECT_EQ(read.wait_for(milliseconds(50)), std::future_status::timeout);
    transfers.router.finished(100, OSDROP_ERROR_CONNECTION_CLOSED, {});
    ASSERT_EQ(read.wait_for(milliseconds(2000)), std::future_status::ready);
    EXPECT_EQ(read.get(), 3U);
    EXPECT_TRUE(stream.broken());
    EXPECT_FALSE(network->received(milliseconds(0)).has_value());
}

TEST_F(NetworkDragTest, CancellingStopsWhatStillRunsAndEndsReads) {
    const auto network = make({{.name = "a"}, {.name = "b", .size = 5}});
    ASSERT_TRUE(network->start());
    transfers.router.planned(100, {{.path = "a", .kind = kFile}});
    transfers.router.planned(101, {{.path = "b", .kind = kFile, .size = 5}});
    transfers.router.finished(100, OSDROP_OK, {staging.path() / "0" / "a"});
    ASSERT_TRUE(network->entries(milliseconds(0)).has_value());
    const auto reader = network->open(1);
    std::vector<std::uint8_t> buffer(5);
    auto read = std::async(std::launch::async, [&] { return reader->read(buffer); });
    network->cancel();
    EXPECT_EQ(transfers.cancelled, std::vector<std::uint64_t>{101}) << "only what still runs";
    ASSERT_EQ(read.wait_for(milliseconds(2000)), std::future_status::ready);
    EXPECT_EQ(read.get(), 0U);
}

TEST_F(NetworkDragTest, TheReceivedItemsAreKnownOnceEveryTransferEnded) {
    const auto network = make({{.name = "a"}, {.name = "b"}});
    ASSERT_TRUE(network->start());
    transfers.router.finished(101, OSDROP_OK, {staging.path() / "1" / "b"});
    EXPECT_FALSE(network->received(milliseconds(0)).has_value());
    transfers.router.finished(100, OSDROP_OK, {staging.path() / "0" / "a"});
    EXPECT_EQ(network->received(milliseconds(0)),
              (std::vector<fs::path>{staging.path() / "0" / "a", staging.path() / "1" / "b"}));
}

} // namespace
