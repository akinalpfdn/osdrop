#pragma once

// Files a member sends this PC without a drag (a phone; DEVPLAN Phase 20; docs/PROTOCOL.md,
// "Sending files"). Each item is fetched into one folder (Downloads) and listed as a transfer this
// PC receives, and the sender gets the answer once every item ended. Core calls arrived() and
// finished() on its own thread, which must not call the mesh, so fetching and answering run on a
// worker of this class's own.

#include <osdrop/osdrop.h>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace osdrop::win {

using SenderId = std::array<std::uint8_t, OSDROP_DEVICE_ID_SIZE>;

/// What SentFiles asks of the mesh; the engine's, or a fake.
class SentMesh {
public:
    virtual ~SentMesh() = default;
    /// As osdrop_mesh_receive for item `index` of the send; the transfer, or nullopt.
    [[nodiscard]] virtual std::optional<std::uint64_t>
    receive(const SenderId& device, std::uint64_t send_id, std::uint32_t index,
            const std::filesystem::path& folder) = 0;
    /// As osdrop_mesh_answer_send.
    virtual void answer(const SenderId& device, std::uint64_t send_id,
                        osdrop_send_outcome outcome) = 0;
};

/// A transfer fetching a sent item started: list it (on the worker).
using SentReceiving = std::function<void(std::uint64_t transfer_id, const SenderId& source,
                                         const std::string& name, std::uint64_t size)>;

struct SentItem {
    std::string name;
    std::uint64_t size = 0;
};

class SentFiles {
public:
    /// `folder` empty: sends are declined.
    SentFiles(SentMesh& mesh, SentReceiving receiving, std::filesystem::path folder);
    /// Stops the worker; call stop() earlier when the mesh goes first.
    ~SentFiles();
    SentFiles(const SentFiles&) = delete;
    SentFiles& operator=(const SentFiles&) = delete;
    SentFiles(SentFiles&&) = delete;
    SentFiles& operator=(SentFiles&&) = delete;

    /// From core's thread: a member sent these items.
    void arrived(const SenderId& device, std::uint64_t send_id, std::vector<SentItem> items);
    /// From core's thread: a transfer ended, a sent item's or another one.
    void finished(std::uint64_t transfer_id, osdrop_result result);
    /// Ends the worker after the work queued so far; nothing runs afterwards.
    void stop();

private:
    struct Arrival {
        SenderId device{};
        std::size_t pending = 0; ///< items not ended yet, counted from the start
        bool failed = false;
    };

    void post(std::function<void()> work);
    void run();
    void fetch(const SenderId& device, std::uint64_t send_id, const std::vector<SentItem>& items);
    /// One item ended; answers once the last one did. Takes mutex_ itself.
    void item_ended(std::uint64_t send_id, bool succeeded);

    SentMesh& mesh_;
    SentReceiving receiving_;
    std::filesystem::path folder_;

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> work_;       ///< guarded by mutex_
    bool stopping_ = false;                        ///< guarded by mutex_
    std::map<std::uint64_t, Arrival> arrivals_;    ///< by send ID; guarded by mutex_
    std::map<std::uint64_t, std::uint64_t> sends_; ///< transfer → send; guarded by mutex_
    /// Transfers that ended before they were recorded: whether they succeeded; guarded by mutex_.
    std::map<std::uint64_t, bool> early_;
    std::thread worker_; ///< last: started once everything above exists
};

} // namespace osdrop::win
