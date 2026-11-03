#include "protocol/transfer_messages.hpp"
#include "temp_folder.hpp"
#include "transfer/manifest.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace protocol = osdrop::protocol;
namespace transfer = osdrop::transfer;
using osdrop::Error;
using osdrop::test::pattern;
using osdrop::test::TempFolder;
using osdrop::test::write_file;
using protocol::EntryKind;
using protocol::ManifestEntry;

std::vector<std::string> paths_of(const transfer::SourceTree& tree) {
    std::vector<std::string> paths;
    for (const ManifestEntry& entry : tree.entries) {
        paths.push_back(entry.path);
    }
    return paths;
}

bool can_link(const TempFolder& folder) {
    std::error_code error;
    fs::create_symlink("x", folder / ".probe", error);
    fs::remove(folder / ".probe", error);
    return !error;
}

ManifestEntry file(std::string path, std::uint64_t size = 1) {
    return {.path = std::move(path),
            .kind = EntryKind::File,
            .executable = false,
            .byte_count = size,
            .modified_us = 0};
}

ManifestEntry folder_entry(std::string path) {
    return {.path = std::move(path),
            .kind = EntryKind::Folder,
            .executable = false,
            .byte_count = 0,
            .modified_us = 0};
}

ManifestEntry link(std::string path, std::uint64_t size) {
    return {.path = std::move(path),
            .kind = EntryKind::Link,
            .executable = false,
            .byte_count = size,
            .modified_us = 0};
}

TEST(TransferManifest, ScansTreesParentsFirstWithoutFollowingLinks) {
    const TempFolder folder;
    write_file(folder / "Project/b.txt", "bee");
    write_file(folder / "Project/a/deep/x.bin", pattern(1000));
    write_file(folder / "Project/a/y.bin", pattern(10));
    write_file(folder / "outside.txt", "not part of the item");
    const bool links = can_link(folder);
    if (links) {
        fs::create_symlink("../b.txt", folder / "Project/a/to-b");
        fs::create_directory_symlink("..", folder / "Project/a/loop"); // never walked into
    }

    const std::vector<fs::path> roots{folder / "Project"};
    const auto tree = transfer::scan(roots);
    ASSERT_TRUE(tree.has_value());
    std::vector<std::string> expected{"Project", "Project/a", "Project/b.txt", "Project/a/deep"};
    if (links) {
        expected.emplace_back("Project/a/loop");
        expected.emplace_back("Project/a/to-b");
    }
    expected.emplace_back("Project/a/y.bin");
    expected.emplace_back("Project/a/deep/x.bin");
    EXPECT_EQ(paths_of(tree.value()), expected);
    EXPECT_EQ(tree.value().total_bytes, 1013U);
    EXPECT_EQ(tree.value().skipped, 0U);
    EXPECT_EQ(tree.value().entries.size(), tree.value().sources.size());
    if (links) {
        const auto& to_b = tree.value().entries.at(5);
        EXPECT_EQ(to_b.kind, EntryKind::Link);
        EXPECT_EQ(to_b.byte_count, 8U);
        EXPECT_EQ(tree.value().sources.at(5).link_target, "../b.txt");
    }
}

TEST(TransferManifest, NamesTravelInNfcAndDuplicatesAreNumbered) {
    const TempFolder folder;
    write_file(folder / "one/Cafe\xCC\x81.txt", "decomposed"); // "Café" in NFD
    write_file(folder / "two/report.txt", "first");
    write_file(folder / "three/REPORT.txt", "second");

    const std::vector<fs::path> roots{folder / "one/Cafe\xCC\x81.txt", folder / "two/report.txt",
                                      folder / "three/REPORT.txt"};
    const auto tree = transfer::scan(roots);
    ASSERT_TRUE(tree.has_value());
    EXPECT_EQ(paths_of(tree.value()),
              (std::vector<std::string>{"Caf\xC3\xA9.txt", "report.txt", "REPORT (2).txt"}));
}

TEST(TransferManifest, RootsThatAreLinksAreFollowed) {
    const TempFolder folder;
    write_file(folder / "real.txt", "content");
    if (!can_link(folder)) {
        GTEST_SKIP() << "this account cannot create symbolic links";
    }
    fs::create_symlink(folder / "real.txt", folder / "alias.txt");
    const std::vector<fs::path> roots{folder / "alias.txt"};
    const auto tree = transfer::scan(roots);
    ASSERT_TRUE(tree.has_value());
    ASSERT_EQ(tree.value().entries.size(), 1U);
    EXPECT_EQ(tree.value().entries[0].kind, EntryKind::File);
    EXPECT_EQ(tree.value().entries[0].byte_count, 7U);
}

TEST(TransferManifest, ScanFailsForMissingRootsAndHugeTrees) {
    const TempFolder folder;
    for (int i = 0; i < 5; ++i) {
        write_file(folder / ("many/" + std::to_string(i)), "x");
    }
    const std::vector<fs::path> missing{folder / "nothing"};
    EXPECT_EQ(transfer::scan(missing).error(), Error::NotFound);
    const std::vector<fs::path> many{folder / "many"};
    EXPECT_EQ(transfer::scan(many, 5).error(), Error::TooLarge);
    EXPECT_TRUE(transfer::scan(many, 6).has_value());
}

TEST(TransferManifest, MessagesSplitByCountAndSize) {
    std::vector<ManifestEntry> entries{folder_entry("root")};
    for (int i = 0; i < 5000; ++i) {
        entries.push_back(file("root/" + std::to_string(i)));
    }
    const std::string long_name(250, 'n');
    for (int i = 0; i < 400; ++i) {
        entries.push_back(file("root/" + long_name + "/" + std::to_string(i))); // ~280 bytes each
    }
    const auto parts = transfer::manifest_messages(9, entries);
    ASSERT_GE(parts.size(), 3U);
    std::size_t total = 0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        EXPECT_EQ(parts[i].is_last, i + 1 == parts.size());
        EXPECT_EQ(parts[i].request_id, 9U);
        EXPECT_LE(parts[i].entries.size(), protocol::kMaxManifestEntries);
        const auto encoded = protocol::encode_transfer(parts[i]);
        ASSERT_TRUE(encoded.has_value()) << i;
        EXPECT_EQ(encoded.value().size(), transfer::manifest_size(parts[i].entries));
        EXPECT_LE(encoded.value().size(), protocol::kMaxTransferMessageSize);
        total += parts[i].entries.size();
    }
    EXPECT_EQ(total, entries.size());
}

TEST(TransferManifest, CollectorMapsNamesAndRefusesBadOrder) {
    transfer::ManifestCollector collector(transfer::TargetPlatform::Windows);
    ASSERT_TRUE(collector
                    .add({.request_id = 1,
                          .is_last = false,
                          .entries = {folder_entry("Q: A?"), file("Q: A?/con.txt", 5),
                                      file("Q: A?/Notes.", 7)}})
                    .has_value());
    EXPECT_FALSE(collector.complete());
    ASSERT_TRUE(collector.add({.request_id = 1, .is_last = true, .entries = {file("notes", 1)}})
                    .has_value());
    EXPECT_TRUE(collector.complete());
    const auto& plan = collector.plan();
    ASSERT_EQ(plan.entries.size(), 4U);
    EXPECT_EQ(plan.entries[0].target, "Q： A？");
    EXPECT_EQ(plan.entries[1].target, "Q： A？/con_.txt");
    EXPECT_EQ(plan.entries[2].target, "Q： A？/Notes．");
    EXPECT_EQ(plan.total_bytes, 13U);
    EXPECT_EQ(plan.file_count, 3U);
    EXPECT_EQ(collector.add({.request_id = 1, .is_last = true, .entries = {file("x")}}).error(),
              Error::InvalidState);

    transfer::ManifestCollector orphan(transfer::TargetPlatform::MacOS);
    EXPECT_EQ(orphan.add({.request_id = 1, .is_last = true, .entries = {file("a/b")}}).error(),
              Error::InvalidArgument);
    transfer::ManifestCollector under_file(transfer::TargetPlatform::MacOS);
    EXPECT_EQ(
        under_file.add({.request_id = 1, .is_last = true, .entries = {file("a"), file("a/b")}})
            .error(),
        Error::InvalidArgument);
    transfer::ManifestCollector twice(transfer::TargetPlatform::MacOS);
    EXPECT_EQ(
        twice.add({.request_id = 1, .is_last = true, .entries = {file("a"), file("a")}}).error(),
        Error::InvalidArgument);
    transfer::ManifestCollector small(transfer::TargetPlatform::MacOS, 1);
    EXPECT_EQ(
        small.add({.request_id = 1, .is_last = true, .entries = {file("a"), file("b")}}).error(),
        Error::TooLarge);
}

TEST(TransferManifest, LinksKeepPointingAtTheSameEntryAfterMapping) {
    transfer::ManifestCollector collector(transfer::TargetPlatform::Windows);
    ASSERT_TRUE(
        collector
            .add({.request_id = 1,
                  .is_last = true,
                  .entries = {folder_entry("Item"), folder_entry("Item/a"), folder_entry("Item/b?"),
                              file("Item/b?/data:1"), link("Item/a/l1", 12), link("Item/a/l2", 9),
                              link("Item/a/l3", 2), file("Other")}})
            .has_value());
    const auto& plan = collector.plan();
    const std::size_t l1 = plan.index_of.at("Item/a/l1");
    EXPECT_EQ(transfer::mapped_link_target(plan, l1, "../b?/data:1"), "../b？/data：1");
    EXPECT_EQ(transfer::mapped_link_target(plan, l1, R"(..\b?\data:1)"), "../b？/data：1");
    EXPECT_EQ(transfer::mapped_link_target(plan, l1, "./../../Item/a"), ".");
    EXPECT_EQ(transfer::mapped_link_target(plan, l1, "../.."), std::nullopt);       // leaves it
    EXPECT_EQ(transfer::mapped_link_target(plan, l1, "../../Other"), std::nullopt); // other item
    EXPECT_EQ(transfer::mapped_link_target(plan, l1, "../missing"), std::nullopt);
    EXPECT_EQ(transfer::mapped_link_target(plan, l1, "/etc/passwd"), std::nullopt);
    EXPECT_EQ(transfer::mapped_link_target(plan, l1, "C:/Windows"), std::nullopt);
    EXPECT_EQ(transfer::mapped_link_target(plan, l1, ""), std::nullopt);
}

} // namespace
