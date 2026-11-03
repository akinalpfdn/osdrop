#include "crypto/primitives.hpp"
#include "temp_folder.hpp"
#include "transfer/files.hpp"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace transfer = osdrop::transfer;
using osdrop::Error;
using osdrop::test::listing;
using osdrop::test::pattern;
using osdrop::test::read_file;
using osdrop::test::TempFolder;
using osdrop::test::write_file;

osdrop::crypto::ContentHasher::Digest digest_of(std::span<const std::uint8_t> bytes) {
    osdrop::crypto::ContentHasher hasher;
    hasher.update(bytes);
    return hasher.finish();
}

constexpr std::int64_t kTime = 1'700'000'000'123'456; // 2023-11-14, with microseconds

TEST(TransferFiles, WriterVerifiesAndMovesIntoPlaceWithoutReplacing) {
    const TempFolder folder;
    write_file(folder / "report.pdf", "already here");
    const auto bytes = pattern(200'000);

    auto writer = transfer::FileWriter::create(folder / "report.pdf", "1-0");
    ASSERT_TRUE(writer.has_value());
    EXPECT_TRUE(writer.value().write(std::span(bytes).first(70'000)).has_value());
    EXPECT_TRUE(writer.value().write(std::span(bytes).subspan(70'000)).has_value());
    const auto placed = writer.value().finish(bytes.size(), digest_of(bytes), kTime, false);
    ASSERT_TRUE(placed.has_value());

    EXPECT_EQ(placed.value(), folder / "report (2).pdf");
    EXPECT_EQ(read_file(placed.value()), bytes);
    EXPECT_EQ(transfer::modified_us(placed.value()).value_or(0) / 1000, kTime / 1000);
    EXPECT_EQ(listing(folder.path()), (std::vector<std::string>{"report (2).pdf", "report.pdf"}));
}

TEST(TransferFiles, WriterRemovesWhatFailsTheCheckOrIsAbandoned) {
    const TempFolder folder;
    const auto bytes = pattern(1000);
    {
        auto corrupt = transfer::FileWriter::create(folder / "a.bin", "1-0");
        ASSERT_TRUE(corrupt.has_value());
        ASSERT_TRUE(corrupt.value().write(bytes).has_value());
        auto other = bytes;
        other[500] ^= 1U;
        const auto placed = corrupt.value().finish(bytes.size(), digest_of(other), kTime, false);
        ASSERT_FALSE(placed.has_value());
        EXPECT_EQ(placed.error(), Error::IntegrityFailed);

        auto short_file = transfer::FileWriter::create(folder / "b.bin", "1-1");
        ASSERT_TRUE(short_file.has_value());
        ASSERT_TRUE(short_file.value().write(std::span(bytes).first(999)).has_value());
        EXPECT_EQ(short_file.value().finish(bytes.size(), digest_of(bytes), kTime, false).error(),
                  Error::IntegrityFailed);

        auto abandoned = transfer::FileWriter::create(folder / "c.bin", "1-2");
        ASSERT_TRUE(abandoned.has_value());
        ASSERT_TRUE(abandoned.value().write(bytes).has_value());
        EXPECT_EQ(listing(folder.path()).size(), 1U); // only the abandoned temporary
    }
    EXPECT_TRUE(listing(folder.path()).empty());
}

TEST(TransferFiles, EmptyFilesAndExecutableFilesArriveAsSent) {
    const TempFolder folder;
    auto empty = transfer::FileWriter::create(folder / "empty", "1-0");
    ASSERT_TRUE(empty.has_value());
    const auto placed = empty.value().finish(0, digest_of({}), kTime, false);
    ASSERT_TRUE(placed.has_value());
    EXPECT_TRUE(read_file(placed.value()).empty());

    const auto script = pattern(10);
    auto tool = transfer::FileWriter::create(folder / "tool", "1-1");
    ASSERT_TRUE(tool.has_value());
    ASSERT_TRUE(tool.value().write(script).has_value());
    const auto tool_path = tool.value().finish(script.size(), digest_of(script), kTime, true);
    ASSERT_TRUE(tool_path.has_value());
#ifdef _WIN32
    EXPECT_FALSE(transfer::is_executable(tool_path.value()));
#else
    EXPECT_TRUE(transfer::is_executable(tool_path.value()));
    EXPECT_FALSE(transfer::is_executable(placed.value()));
#endif
}

TEST(TransferFiles, ReaderReadsRangesAndStopsAtTheEnd) {
    const TempFolder folder;
    const auto bytes = pattern(5000);
    write_file(folder / "data", bytes);
    auto reader = transfer::FileReader::open(folder / "data");
    ASSERT_TRUE(reader.has_value());

    std::array<std::uint8_t, 3000> buffer{};
    EXPECT_EQ(reader.value().read(4000, buffer).value(), 1000U);
    EXPECT_TRUE(std::equal(buffer.begin(), buffer.begin() + 1000, bytes.begin() + 4000));
    EXPECT_EQ(reader.value().read(5000, buffer).value(), 0U);
    EXPECT_EQ(reader.value().read(0, buffer).value(), buffer.size());
    EXPECT_TRUE(std::equal(buffer.begin(), buffer.end(), bytes.begin()));
    EXPECT_EQ(reader.value().read(3000, buffer).value(), 2000U);
    EXPECT_TRUE(std::equal(buffer.begin(), buffer.begin() + 2000, bytes.begin() + 3000));

    EXPECT_EQ(transfer::FileReader::open(folder / "missing").error(), Error::NotFound);
}

TEST(TransferFiles, FoldersAreNumberedWhenTheNameIsTaken) {
    const TempFolder folder;
    write_file(folder / "Photos.2024", "a file with the folder's name");
    ASSERT_TRUE(fs::create_directory(folder / "Photos.2024 (2)"));
    const auto created = transfer::create_unique_folder(folder.path(), "Photos.2024");
    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(created.value(), folder / "Photos.2024 (3)");
    EXPECT_TRUE(fs::is_directory(created.value()));
}

TEST(TransferFiles, LongPathsAndUnicodeNamesWork) {
    const TempFolder folder;
    // Over 260 characters in all: Windows needs the \\?\ form for it.
    std::string relative;
    for (int i = 0; i < 6; ++i) {
        relative += std::string(50, static_cast<char>('a' + i)) + "/";
    }
    relative += "résumé ✓ 😀.txt";
    const fs::path target = folder / relative;
    ASSERT_GT(transfer::utf8_of(target).size(), 300U);
    ASSERT_TRUE(fs::create_directories(transfer::native_path(target.parent_path())));

    const auto bytes = pattern(100);
    auto writer = transfer::FileWriter::create(target, "1-0");
    ASSERT_TRUE(writer.has_value());
    ASSERT_TRUE(writer.value().write(bytes).has_value());
    const auto placed = writer.value().finish(bytes.size(), digest_of(bytes), kTime, false);
    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(read_file(placed.value()), bytes);
    EXPECT_EQ(transfer::modified_us(placed.value()).value_or(0) / 1000, kTime / 1000);
    EXPECT_TRUE(transfer::free_space(placed.value()).value_or(0) > 0);
}

TEST(TransferFiles, LinksAreCreatedWithRelativeTargets) {
    const TempFolder folder;
    write_file(folder / "dir/target.txt", "hello");
    const auto created = transfer::create_link(folder / "dir/link", "target.txt", false);
    if (!created.has_value() && created.error() == Error::NotPermitted) {
        GTEST_SKIP() << "this account cannot create symbolic links";
    }
    ASSERT_TRUE(created.has_value());
    EXPECT_TRUE(fs::is_symlink(fs::symlink_status(folder / "dir/link")));
    EXPECT_EQ(read_file(folder / "dir/link"), read_file(folder / "dir/target.txt"));
    EXPECT_TRUE(transfer::link_modified_us(folder / "dir/link").has_value());
}

} // namespace
