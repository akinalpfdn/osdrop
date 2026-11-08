// The shared clipboard between group members over the simulated network: copies are announced,
// the newest wins everywhere, contents and files are fetched within limits, and the clipboard
// permission keeps copies at home.

#include "clipboard/clipboard.hpp"
#include "group_simulation.hpp"
#include "mesh_simulation.hpp"
#include "temp_folder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace clipboard = osdrop::clipboard;
namespace protocol = osdrop::protocol;
using osdrop::Error;
using osdrop::test::form_group;
using osdrop::test::listing;
using osdrop::test::pattern;
using osdrop::test::read_file;
using osdrop::test::SimulatedMeshNetwork;
using osdrop::test::SimulatedNode;
using osdrop::test::TempFolder;
using osdrop::test::write_file;
using protocol::ClipboardFormat;
using std::chrono::milliseconds;

std::vector<std::uint8_t> bytes(const std::string& text) {
    return {text.begin(), text.end()};
}

protocol::ObjectRef item_of(std::uint64_t clip, ClipboardFormat format) {
    return {.kind = protocol::ObjectKind::Clipboard,
            .object_id = clip,
            .item_index = static_cast<std::uint32_t>(format)};
}

class SharedClipboard : public ::testing::Test {
protected:
    void form(std::size_t count) {
        network.capture_wire = false;
        form_group(network, count);
    }

    SimulatedNode& node(std::size_t index) { return network.node(index); }

    std::uint64_t copy(std::size_t at, std::vector<clipboard::LocalFormat> formats,
                       std::vector<std::filesystem::path> files, std::int64_t wall_us) {
        const auto clip = node(at).clipboard->copied(std::move(formats), std::move(files), wall_us);
        EXPECT_TRUE(clip.has_value());
        node(at).mesh.flush(network.now);
        network.run_for(milliseconds(20));
        return clip ? clip.value() : 0;
    }

    /// Fetches one format at `at` from the device that copied it; the outcome.
    osdrop::transfer::ContentOutcome fetch(std::size_t at, std::size_t from, std::uint64_t clip,
                                           ClipboardFormat format, std::uint64_t max_bytes) {
        const std::size_t before = node(at).transfer_observer.contents.size();
        const auto id = node(at).transfers->fetch_content(node(from).mesh.device_id(),
                                                          item_of(clip, format), max_bytes);
        EXPECT_TRUE(id.has_value());
        node(at).mesh.flush(network.now);
        for (int i = 0; i < 2'000 && node(at).transfer_observer.contents.size() == before; ++i) {
            network.step();
        }
        EXPECT_GT(node(at).transfer_observer.contents.size(), before);
        return node(at).transfer_observer.contents.back();
    }

    SimulatedMeshNetwork network;
};

TEST_F(SharedClipboard, ACopyIsAnnouncedAndItsContentsFetchedOnDemand) {
    form(3);
    const std::uint64_t clip =
        copy(0,
             {{.format = ClipboardFormat::PlainText, .bytes = bytes("hello")},
              {.format = ClipboardFormat::Html, .bytes = bytes("<b>hello</b>")}},
             {}, 1'700'000'000'000'000);
    EXPECT_EQ(clip, 1'700'000'000'000'000U);
    for (const std::size_t other : {std::size_t{1}, std::size_t{2}}) {
        ASSERT_EQ(node(other).clipboard_observer.clips.size(), 1U) << other;
        const clipboard::RemoteClip& seen = node(other).clipboard_observer.clips.back();
        EXPECT_EQ(seen.device, node(0).mesh.device_id());
        EXPECT_EQ(seen.clip_id, clip);
        EXPECT_EQ(seen.formats, (std::vector<protocol::AnnouncedFormat>{
                                    {.format = ClipboardFormat::PlainText, .byte_count = 5},
                                    {.format = ClipboardFormat::Html, .byte_count = 12}}));
        EXPECT_EQ(node(other).clipboard->remote(), seen);
    }
    EXPECT_FALSE(node(0).clipboard->remote().has_value());

    const auto text = fetch(1, 0, clip, ClipboardFormat::PlainText, 1024);
    EXPECT_FALSE(text.error.has_value());
    EXPECT_EQ(text.content, bytes("hello"));
    const auto html = fetch(2, 0, clip, ClipboardFormat::Html, 1024);
    EXPECT_EQ(html.content, bytes("<b>hello</b>"));
    // A format the copy does not have.
    EXPECT_EQ(fetch(1, 0, clip, ClipboardFormat::Image, 1024).error, Error::NotFound);
}

TEST_F(SharedClipboard, ContentsAboveTheLimitAreRefusedAndLargeOnesArriveWhole) {
    form(2);
    const auto image = pattern(3'000'000);
    const std::uint64_t clip =
        copy(0, {{.format = ClipboardFormat::Image, .bytes = image}}, {}, 1'000);
    EXPECT_EQ(fetch(1, 0, clip, ClipboardFormat::Image, 2'999'999).error, Error::TooLarge);
    const auto whole = fetch(1, 0, clip, ClipboardFormat::Image, 3'000'000);
    EXPECT_FALSE(whole.error.has_value());
    EXPECT_EQ(whole.content, image);
}

TEST_F(SharedClipboard, TheNewestCopyWinsOnEveryMember) {
    form(3);
    // Two copies at the same moment: the higher device ID wins everywhere.
    ASSERT_TRUE(
        node(0)
            .clipboard
            ->copied({{.format = ClipboardFormat::PlainText, .bytes = bytes("zero")}}, {}, 5'000)
            .has_value());
    ASSERT_TRUE(
        node(1)
            .clipboard
            ->copied({{.format = ClipboardFormat::PlainText, .bytes = bytes("one")}}, {}, 5'000)
            .has_value());
    node(0).mesh.flush(network.now);
    node(1).mesh.flush(network.now);
    network.run_for(milliseconds(20));
    const std::size_t winner =
        node(0).mesh.device_id() > node(1).mesh.device_id() ? std::size_t{0} : std::size_t{1};
    const std::size_t loser = 1 - winner;
    EXPECT_FALSE(node(winner).clipboard->remote().has_value()) << "its own copy stays newest";
    ASSERT_TRUE(node(loser).clipboard->remote().has_value());
    EXPECT_EQ(node(loser).clipboard->remote()->device, node(winner).mesh.device_id());
    ASSERT_TRUE(node(2).clipboard->remote().has_value());
    EXPECT_EQ(node(2).clipboard->remote()->device, node(winner).mesh.device_id());
    // The loser's copy was withdrawn when the winner's arrived.
    EXPECT_EQ(fetch(2, loser, 5'000, ClipboardFormat::PlainText, 64).error, Error::NotFound);

    // A copy made later wins even with a clock that runs behind.
    const std::uint64_t later =
        copy(2, {{.format = ClipboardFormat::PlainText, .bytes = bytes("two")}}, {}, 10);
    EXPECT_GT(later, 5'000U);
    for (const std::size_t other : {std::size_t{0}, std::size_t{1}}) {
        ASSERT_TRUE(node(other).clipboard->remote().has_value());
        EXPECT_EQ(node(other).clipboard->remote()->device, node(2).mesh.device_id());
        EXPECT_EQ(node(other).clipboard->remote()->clip_id, later);
    }
}

TEST_F(SharedClipboard, CopiedFilesAreAnnouncedWithTheirSizeAndFetchedWithinALimit) {
    form(2);
    const TempFolder source;
    const TempFolder destination;
    write_file(source / "Photos/a.jpg", pattern(300'000));
    write_file(source / "notes.txt", "notes");
    const std::uint64_t clip =
        copy(0, {{.format = ClipboardFormat::PlainText, .bytes = bytes("Photos notes.txt")}},
             {source / "Photos", source / "notes.txt"}, 2'000);
    network.run_for(milliseconds(20));
    ASSERT_EQ(node(1).clipboard_observer.clips.size(), 1U) << "announced once the scan is done";
    EXPECT_EQ(node(1).clipboard_observer.clips.back().formats,
              (std::vector<protocol::AnnouncedFormat>{
                  {.format = ClipboardFormat::PlainText, .byte_count = 16},
                  {.format = ClipboardFormat::Files, .byte_count = 300'005}}));

    const auto files = item_of(clip, ClipboardFormat::Files);
    const auto device = node(0).mesh.device_id();
    ASSERT_TRUE(node(1).transfers->receive(device, files, destination.path(), 300'004).has_value());
    node(1).mesh.flush(network.now);
    while (node(1).transfer_observer.finished.empty()) {
        network.step();
    }
    EXPECT_EQ(node(1).transfer_observer.finished.back().error, Error::TooLarge);
    EXPECT_TRUE(listing(destination.path()).empty());

    ASSERT_TRUE(node(1).transfers->receive(device, files, destination.path(), 300'005).has_value());
    node(1).mesh.flush(network.now);
    while (node(1).transfer_observer.finished.size() < 2) {
        network.step();
    }
    ASSERT_FALSE(node(1).transfer_observer.finished.back().error.has_value());
    EXPECT_EQ(listing(destination.path()),
              (std::vector<std::string>{"Photos", "Photos/a.jpg", "notes.txt"}));
    EXPECT_EQ(read_file(destination / "Photos/a.jpg"), pattern(300'000));
}

TEST_F(SharedClipboard, TheClipboardPermissionKeepsCopiesAtHome) {
    form(2);
    ASSERT_TRUE(node(1)
                    .group
                    ->set_permissions(node(0).mesh.device_id(),
                                      {.control = true, .files = true, .clipboard = false},
                                      network.now)
                    .has_value());
    network.run_for(milliseconds(20));
    copy(0, {{.format = ClipboardFormat::PlainText, .bytes = bytes("not for you")}}, {}, 1'000);
    EXPECT_TRUE(node(1).clipboard_observer.clips.empty()) << "refused on arrival";
    copy(1, {{.format = ClipboardFormat::PlainText, .bytes = bytes("stays here")}}, {}, 2'000);
    EXPECT_TRUE(node(0).clipboard_observer.clips.empty()) << "never sent";
}

TEST_F(SharedClipboard, AFetchFromAComputerThatLeftEndsAtOnce) {
    form(2);
    const std::uint64_t clip =
        copy(0, {{.format = ClipboardFormat::PlainText, .bytes = bytes("hello")}}, {}, 1'000);
    ASSERT_EQ(node(1).clipboard_observer.clips.size(), 1U);
    node(0).mesh.disconnect(node(1).mesh.device_id(), protocol::ByeReason::UserDisconnected,
                            network.now);
    network.run_for(milliseconds(20));
    EXPECT_EQ(
        node(1)
            .transfers
            ->fetch_content(node(0).mesh.device_id(), item_of(clip, ClipboardFormat::PlainText), 64)
            .error(),
        Error::InvalidState);
}

TEST_F(SharedClipboard, CopiesAreChecked) {
    form(2);
    EXPECT_EQ(node(0).clipboard->copied({}, {}, 1).error(), Error::InvalidArgument);
    EXPECT_EQ(node(0)
                  .clipboard
                  ->copied({{.format = ClipboardFormat::PlainText, .bytes = bytes("a")},
                            {.format = ClipboardFormat::PlainText, .bytes = bytes("b")}},
                           {}, 1)
                  .error(),
              Error::InvalidArgument);
    EXPECT_EQ(
        node(0).clipboard->copied({{.format = ClipboardFormat::Files, .bytes = {}}}, {}, 1).error(),
        Error::InvalidArgument);
    EXPECT_EQ(node(0).clipboard->copied({}, {"relative/path"}, 1).error(), Error::InvalidArgument);
}

} // namespace
