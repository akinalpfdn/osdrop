// Files a phone sends this PC: every item is fetched into the folder, the answer waits for the
// last one, a failed item fails the send, and without a folder the send is declined.
#include <osdrop/win/sent_files.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace {

using osdrop::win::SenderId;
using osdrop::win::SentFiles;
using osdrop::win::SentItem;

struct Answer {
    std::uint64_t send_id = 0;
    osdrop_send_outcome outcome = OSDROP_SEND_FAILED;
};

class FakeMesh final : public osdrop::win::SentMesh {
public:
    std::optional<std::uint64_t> receive(const SenderId& /*device*/, std::uint64_t /*send_id*/,
                                         std::uint32_t index,
                                         const std::filesystem::path& folder) override {
        const std::scoped_lock lock(mutex_);
        folders.push_back(folder);
        if (index == refused_index) {
            return std::nullopt;
        }
        return 100 + index;
    }
    void answer(const SenderId& /*device*/, std::uint64_t send_id,
                osdrop_send_outcome outcome) override {
        {
            const std::scoped_lock lock(mutex_);
            answers.push_back({.send_id = send_id, .outcome = outcome});
        }
        changed_.notify_all();
    }
    /// The answers once `count` arrived, or what there is after two seconds.
    std::vector<Answer> wait_for_answers(std::size_t count) {
        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, std::chrono::seconds(2), [&] { return answers.size() >= count; });
        return answers;
    }

    std::uint32_t refused_index = UINT32_MAX;
    std::vector<std::filesystem::path> folders;
    std::vector<Answer> answers;

private:
    std::mutex mutex_;
    std::condition_variable changed_;
};

const SenderId kPhone{7};
const std::vector<SentItem> kTwoItems{{.name = "a.jpg", .size = 1}, {.name = "b.apk", .size = 2}};

TEST(SentFiles, TheAnswerWaitsForEveryItem) {
    FakeMesh mesh;
    std::mutex listed_mutex;
    std::vector<std::string> listed;
    SentFiles files(
        mesh,
        [&](std::uint64_t, const SenderId&, const std::string& name, std::uint64_t) {
            const std::scoped_lock lock(listed_mutex);
            listed.push_back(name);
        },
        "/downloads");
    files.arrived(kPhone, 9, kTwoItems);
    files.finished(100, OSDROP_OK);
    files.finished(555, OSDROP_OK); // another transfer: no effect
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(mesh.wait_for_answers(0).empty()) << "one item is still on its way";
    files.finished(101, OSDROP_OK);
    const auto answers = mesh.wait_for_answers(1);
    ASSERT_EQ(answers.size(), 1U);
    EXPECT_EQ(answers[0].send_id, 9U);
    EXPECT_EQ(answers[0].outcome, OSDROP_SEND_RECEIVED);
    files.stop();
    EXPECT_EQ(mesh.folders, (std::vector<std::filesystem::path>{"/downloads", "/downloads"}));
    EXPECT_EQ(listed, (std::vector<std::string>{"a.jpg", "b.apk"}));
}

TEST(SentFiles, AFailedOrRefusedItemFailsTheSend) {
    FakeMesh mesh;
    mesh.refused_index = 1;
    SentFiles files(
        mesh, [](std::uint64_t, const SenderId&, const std::string&, std::uint64_t) {},
        "/downloads");
    files.finished(100, OSDROP_OK); // ends before its start is recorded
    files.arrived(kPhone, 4, kTwoItems);
    const auto answers = mesh.wait_for_answers(1);
    ASSERT_EQ(answers.size(), 1U);
    EXPECT_EQ(answers[0].outcome, OSDROP_SEND_FAILED);
}

TEST(SentFiles, WithoutAFolderSendsAreDeclined) {
    FakeMesh mesh;
    SentFiles files(mesh, [](std::uint64_t, const SenderId&, const std::string&, std::uint64_t) {},
                    {});
    files.arrived(kPhone, 3, kTwoItems);
    const auto answers = mesh.wait_for_answers(1);
    ASSERT_EQ(answers.size(), 1U);
    EXPECT_EQ(answers[0].outcome, OSDROP_SEND_DECLINED);
    EXPECT_TRUE(mesh.folders.empty());
}

} // namespace
