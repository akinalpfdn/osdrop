// The shared clipboard on the PC with a clipboard and a mesh in memory: what is reported, what is
// fetched within the limits, what is written, and the notices.
#include <osdrop/win/clipboard_sync.hpp>

#include <gtest/gtest.h>

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace {

using osdrop::win::ClipboardLimits;
using osdrop::win::ClipboardNotice;
using osdrop::win::ClipboardSync;
using osdrop::win::ClipContent;
using osdrop::win::ClipData;
using osdrop::win::ClipDevice;

constexpr ClipDevice kMac{1, 2, 3};

std::vector<std::uint8_t> bytes(const std::string& text) {
    return {text.begin(), text.end()};
}

/// Runs posted work when the test drains it, as the clipboard thread would.
class FakeHost final : public osdrop::win::ClipboardHost {
public:
    std::vector<ClipData> written;
    std::vector<std::optional<std::string>> kept;
    int folders = 0;
    bool folders_fail = false;

    void post(std::function<void()> work) override { queue_.push_back(std::move(work)); }
    void write(const ClipData& data) override { written.push_back(data); }
    std::optional<std::string> make_staging_folder() override {
        if (folders_fail) {
            return std::nullopt;
        }
        return "C:/staging/" + std::to_string(++folders);
    }
    void clear_staging(const std::optional<std::string>& folder) override {
        kept.push_back(folder);
    }

    void drain() {
        while (!queue_.empty()) {
            auto work = std::move(queue_.front());
            queue_.pop_front();
            work();
        }
    }

private:
    std::deque<std::function<void()>> queue_;
};

/// Records requests and hands out IDs; results are delivered as core would.
class FakeMesh final : public osdrop::win::ClipboardMesh {
public:
    std::vector<ClipData> copies;
    std::vector<std::tuple<std::uint64_t, osdrop_object_ref, std::uint64_t>> fetches;
    std::vector<std::tuple<std::uint64_t, std::string, std::uint64_t>> receives;
    osdrop_result refusal = OSDROP_OK;

    osdrop_result copied(const ClipData& data) override {
        copies.push_back(data);
        return OSDROP_OK;
    }
    osdrop_result fetch_content(const ClipDevice& /*device*/, const osdrop_object_ref& item,
                                std::uint64_t max_bytes, std::uint64_t& id) override {
        if (refusal != OSDROP_OK) {
            return refusal;
        }
        id = next_++;
        fetches.emplace_back(id, item, max_bytes);
        return OSDROP_OK;
    }
    osdrop_result receive(const ClipDevice& /*device*/, const osdrop_object_ref& /*item*/,
                          const std::string& folder, std::uint64_t max_bytes,
                          std::uint64_t& id) override {
        id = next_++;
        receives.emplace_back(id, folder, max_bytes);
        return OSDROP_OK;
    }

private:
    std::uint64_t next_ = 1;
};

class ClipboardSyncTest : public ::testing::Test {
protected:
    ClipboardSyncTest() {
        sync.attach(mesh);
        host.drain();
    }

    void connect() {
        sync.peer_connected(kMac, "Mac");
        host.drain();
    }

    void announce(std::uint64_t clip, std::vector<osdrop_announced_format> formats) {
        sync.clipboard_changed(kMac, clip, std::move(formats));
        host.drain();
    }

    FakeHost host;
    FakeMesh mesh;
    std::vector<std::tuple<ClipboardNotice, ClipDevice, std::string>> notices;
    ClipboardSync sync{
        host,
        [this](ClipboardNotice notice, const ClipDevice& device, const std::string& name) {
            notices.emplace_back(notice, device, name);
        },
        ClipboardLimits{.text = 100, .image = 100, .files = 1000}};
};

TEST_F(ClipboardSyncTest, CopiesAreReportedOnlyWhileAMemberIsConnectedAndWithinLimits) {
    sync.copied(
        {.contents = {{.format = OSDROP_CLIPBOARD_TEXT, .bytes = bytes("alone")}}, .files = {}});
    EXPECT_TRUE(mesh.copies.empty());

    connect();
    sync.copied(
        {.contents = {{.format = OSDROP_CLIPBOARD_TEXT, .bytes = bytes("hello")},
                      {.format = OSDROP_CLIPBOARD_IMAGE, .bytes = std::vector<std::uint8_t>(101)}},
         .files = {"C:/Users/me/report.pdf"}});
    ASSERT_EQ(mesh.copies.size(), 1U);
    EXPECT_EQ(mesh.copies[0],
              (ClipData{.contents = {{.format = OSDROP_CLIPBOARD_TEXT, .bytes = bytes("hello")}},
                        .files = {"C:/Users/me/report.pdf"}}));

    sync.peer_disconnected(kMac);
    host.drain();
    sync.copied(
        {.contents = {{.format = OSDROP_CLIPBOARD_TEXT, .bytes = bytes("gone")}}, .files = {}});
    EXPECT_EQ(mesh.copies.size(), 1U);
}

TEST_F(ClipboardSyncTest, AnotherComputersCopyIsWrittenOnceEverythingArrived) {
    connect();
    announce(7, {{.format = OSDROP_CLIPBOARD_TEXT, .byte_count = 5},
                 {.format = OSDROP_CLIPBOARD_IMAGE, .byte_count = 2},
                 {.format = OSDROP_CLIPBOARD_FILES, .byte_count = 500}});
    ASSERT_EQ(mesh.fetches.size(), 2U);
    ASSERT_EQ(mesh.receives.size(), 1U);
    const auto [text_id, text_item, text_limit] = mesh.fetches[0];
    EXPECT_EQ(text_item.kind, OSDROP_OBJECT_CLIPBOARD);
    EXPECT_EQ(text_item.object_id, 7U);
    EXPECT_EQ(text_item.item_index, static_cast<std::uint32_t>(OSDROP_CLIPBOARD_TEXT));
    EXPECT_EQ(text_limit, 100U);
    const auto [files_id, folder, files_limit] = mesh.receives[0];
    EXPECT_EQ(folder, "C:/staging/1");
    EXPECT_EQ(files_limit, 1000U);

    sync.content_received(text_id, OSDROP_OK, bytes("hello"));
    sync.content_received(std::get<0>(mesh.fetches[1]), OSDROP_OK, {9, 9});
    host.drain();
    EXPECT_TRUE(host.written.empty()) << "the files are still on their way";
    sync.transfer_finished(files_id, OSDROP_OK, {"C:/staging/1/report.pdf"});
    host.drain();
    ASSERT_EQ(host.written.size(), 1U);
    EXPECT_EQ(host.written[0],
              (ClipData{.contents = {{.format = OSDROP_CLIPBOARD_TEXT, .bytes = bytes("hello")},
                                     {.format = OSDROP_CLIPBOARD_IMAGE, .bytes = {9, 9}}},
                        .files = {"C:/staging/1/report.pdf"}}));
    EXPECT_EQ(host.kept.back(), "C:/staging/1") << "the pasted files' folder stays";
    EXPECT_TRUE(notices.empty());
}

TEST_F(ClipboardSyncTest, CopiesTooLargeOrOutOfReachLeaveANotice) {
    connect();
    announce(8, {{.format = OSDROP_CLIPBOARD_FILES, .byte_count = 5000}});
    EXPECT_TRUE(mesh.receives.empty());
    ASSERT_EQ(notices.size(), 1U);
    EXPECT_EQ(notices[0], std::tuple(ClipboardNotice::TooLarge, kMac, std::string("Mac")));

    announce(9, {{.format = OSDROP_CLIPBOARD_TEXT, .byte_count = 3}});
    sync.content_received(std::get<0>(mesh.fetches.back()), OSDROP_ERROR_CONNECTION_CLOSED, {});
    host.drain();
    ASSERT_EQ(notices.size(), 2U);
    EXPECT_EQ(std::get<0>(notices[1]), ClipboardNotice::Unavailable);
    EXPECT_TRUE(host.written.empty());

    mesh.refusal = OSDROP_ERROR_INVALID_STATE; // the computer left before the request
    announce(10, {{.format = OSDROP_CLIPBOARD_TEXT, .byte_count = 3}});
    ASSERT_EQ(notices.size(), 3U);
    EXPECT_EQ(std::get<0>(notices[2]), ClipboardNotice::Unavailable);

    mesh.refusal = OSDROP_OK;
    host.folders_fail = true;
    announce(11, {{.format = OSDROP_CLIPBOARD_FILES, .byte_count = 10}});
    EXPECT_TRUE(mesh.receives.empty()) << "no folder, no request";
}

TEST_F(ClipboardSyncTest, ANewerCopyReplacesOneStillOnItsWay) {
    connect();
    announce(12, {{.format = OSDROP_CLIPBOARD_TEXT, .byte_count = 3}});
    announce(13, {{.format = OSDROP_CLIPBOARD_TEXT, .byte_count = 3}});
    sync.content_received(std::get<0>(mesh.fetches[0]), OSDROP_OK, bytes("old"));
    host.drain();
    EXPECT_TRUE(host.written.empty());
    sync.content_received(std::get<0>(mesh.fetches[1]), OSDROP_OK, bytes("new"));
    host.drain();
    ASSERT_EQ(host.written.size(), 1U);
    EXPECT_EQ(host.written[0].contents[0].bytes, bytes("new"));

    // A copy on this PC wins over one still being fetched.
    announce(14, {{.format = OSDROP_CLIPBOARD_TEXT, .byte_count = 3}});
    sync.copied(
        {.contents = {{.format = OSDROP_CLIPBOARD_TEXT, .bytes = bytes("mine")}}, .files = {}});
    sync.content_received(std::get<0>(mesh.fetches[2]), OSDROP_OK, bytes("late"));
    host.drain();
    EXPECT_EQ(host.written.size(), 1U);
}

} // namespace
