// The portable half of the PC's drag bridge (DEVPLAN Phase 4): file descriptors for dragged
// trees, streams over content that reads front to back, and the strips along the outer edges.
#include <osdrop/win/drag_files.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <vector>

namespace {

namespace drag = osdrop::win::drag;
using osdrop::win::PhysicalMonitor;
using osdrop::win::PixelRect;

TEST(DragFiles, TreesBecomeDescriptorsWithBackslashesParentsFirst) {
    const std::vector<drag::TreeEntry> entries{
        {.path = "Rapor.pdf", .folder = false, .size = 10, .write_time = 7},
        {.path = "Fotoğraflar", .folder = true, .size = 99, .write_time = 0},
        {.path = "Fotoğraflar/Yaz", .folder = true},
        {.path = "Fotoğraflar/Yaz/deniz.jpg", .folder = false, .size = 2048},
    };
    const drag::VirtualFiles described = drag::virtual_files(entries);
    EXPECT_EQ(described.skipped, 0U);
    ASSERT_EQ(described.files.size(), 4U);
    EXPECT_EQ(described.files[0],
              (drag::VirtualFile{
                  .path = u"Rapor.pdf", .folder = false, .size = 10, .write_time = 7, .entry = 0}));
    EXPECT_EQ(described.files[1].size, 0U) << "folders have no size";
    EXPECT_EQ(described.files[3].path, u"Fotoğraflar\\Yaz\\deniz.jpg");
    EXPECT_EQ(described.files[3].entry, 3U);
}

TEST(DragFiles, PathsADescriptorCannotHoldAreLeftOutWithWhatIsBelowThem) {
    const std::string deep(drag::kMaxDescriptorPath + 1, 'a');
    const std::vector<drag::TreeEntry> entries{
        {.path = "kısa.txt"},
        {.path = deep, .folder = true},
        {.path = deep + "/b.txt"},
        {.path = "orphan/c.txt"},
        {.path = "a/../b"},
        {.path = ""},
        {.path = std::string(drag::kMaxDescriptorPath - 1, 'x') + "ğ"},
    };
    const drag::VirtualFiles described = drag::virtual_files(entries);
    ASSERT_EQ(described.files.size(), 2U);
    EXPECT_EQ(described.files[0].path, u"kısa.txt");
    EXPECT_EQ(described.files[1].entry, 6U) << "259 UTF-16 units fit, whatever their bytes";
    EXPECT_EQ(described.skipped, 5U);
}

TEST(DragFiles, Utf8BecomesUtf16AndBrokenBytesAReplacementCharacter) {
    EXPECT_EQ(drag::utf16("Ağaç 🌳"), u"Ağaç 🌳");
    EXPECT_EQ(drag::utf16("a\xFF"
                          "b"),
              u"a�b");
    EXPECT_EQ(drag::utf16("\xC0\x80"), u"��") << "overlong";
    EXPECT_EQ(drag::utf16("\xED\xA0\x80"), u"���") << "a surrogate";
}

/// Content of `size` bytes counting up, read a few bytes at a time; counts how often it opened.
class CountingSource final : public drag::ContentSource {
public:
    explicit CountingSource(std::size_t size, std::size_t step = 3) : size_(size), step_(step) {}

    std::unique_ptr<drag::ContentReader> open(std::size_t /*entry*/) override {
        ++opened;
        return std::make_unique<Reader>(size_, step_);
    }

    int opened = 0;

private:
    class Reader final : public drag::ContentReader {
    public:
        Reader(std::size_t size, std::size_t step) : size_(size), step_(step) {}
        std::size_t read(std::span<std::uint8_t> buffer) override {
            const std::size_t count = std::min({buffer.size(), step_, size_ - next_});
            for (std::size_t i = 0; i < count; ++i) {
                buffer[i] = static_cast<std::uint8_t>(next_ + i);
            }
            next_ += count;
            return count;
        }

    private:
        std::size_t size_;
        std::size_t step_;
        std::size_t next_ = 0;
    };

    std::size_t size_;
    std::size_t step_;
};

TEST(DragFiles, AStreamReadsFrontToBackAndFillsItsBuffer) {
    CountingSource source(10);
    drag::FileStream stream(source, 0, 10);
    std::vector<std::uint8_t> buffer(8);
    EXPECT_EQ(stream.read(buffer), 8U) << "reads on until the buffer is full";
    EXPECT_EQ(buffer[7], 7);
    EXPECT_EQ(stream.read(buffer), 2U);
    EXPECT_EQ(stream.read(buffer), 0U);
    EXPECT_EQ(stream.position(), 10U);
    EXPECT_EQ(source.opened, 1);
}

TEST(DragFiles, SeekingForwardSkipsAndSeekingBackStartsOver) {
    CountingSource source(100);
    drag::FileStream stream(source, 0, 100);
    ASSERT_TRUE(stream.seek(40));
    std::vector<std::uint8_t> one(1);
    ASSERT_EQ(stream.read(one), 1U);
    EXPECT_EQ(one[0], 40);
    EXPECT_EQ(source.opened, 1);
    ASSERT_TRUE(stream.seek(41)) << "where it is";
    EXPECT_EQ(source.opened, 1);
    ASSERT_TRUE(stream.seek(5));
    ASSERT_EQ(stream.read(one), 1U);
    EXPECT_EQ(one[0], 5);
    EXPECT_EQ(source.opened, 2);
    EXPECT_FALSE(stream.seek(101));
    EXPECT_TRUE(stream.seek(100));
    EXPECT_EQ(stream.read(one), 0U);
}

TEST(DragFiles, ContentThatEndsEarlyStopsTheStream) {
    CountingSource source(4);
    drag::FileStream stream(source, 0, 10); // the descriptor promised more than arrives
    std::vector<std::uint8_t> buffer(10);
    EXPECT_EQ(stream.read(buffer), 4U);
    EXPECT_FALSE(stream.seek(8));
}

PhysicalMonitor monitor(std::int32_t left, std::int32_t top, std::int32_t right,
                        std::int32_t bottom) {
    return {.key = {},
            .left = left,
            .top = top,
            .right = right,
            .bottom = bottom,
            .dpi = 96,
            .primary = left == 0 && top == 0};
}

TEST(DragFiles, StripsLineTheEdgesNoOtherMonitorContinues) {
    // A main monitor and a smaller one to its right, lower down.
    const std::vector<PhysicalMonitor> monitors{monitor(0, 0, 1920, 1080),
                                                monitor(1920, 600, 3200, 1400)};
    std::vector<PixelRect> strips = drag::edge_strips(monitors, 4);
    const auto has = [&](PixelRect strip) {
        return std::ranges::find(strips, strip) != strips.end();
    };
    EXPECT_TRUE(has({.left = 1916, .top = 0, .right = 1920, .bottom = 600}))
        << "the main monitor's right edge above the other one";
    EXPECT_FALSE(has({.left = 1916, .top = 0, .right = 1920, .bottom = 1080}));
    EXPECT_TRUE(has({.left = 1920, .top = 1080, .right = 1924, .bottom = 1400}))
        << "the other's left edge below the main one";
    EXPECT_TRUE(has({.left = 3196, .top = 600, .right = 3200, .bottom = 1400}));
    EXPECT_TRUE(has({.left = 0, .top = 0, .right = 4, .bottom = 1080}));
    EXPECT_EQ(strips.size(), 8U) << "four for each monitor, the shared edges cut short";
}

} // namespace
