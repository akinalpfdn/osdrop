#pragma once

// What the transfer engine is configured with, reports and runs its file work on.

#include "mesh/mesh.hpp"
#include "protocol/transfer_messages.hpp"
#include "transfer/manifest.hpp"
#include "util/error.hpp"
#include "util/time.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace osdrop::transfer {

using TransferId = std::uint64_t;

/// Orders object references, for maps.
struct ObjectOrder {
    bool operator()(const protocol::ObjectRef& a, const protocol::ObjectRef& b) const {
        return std::tuple(a.kind, a.object_id, a.item_index) <
               std::tuple(b.kind, b.object_id, b.item_index);
    }
};

struct TransferTunables {
    /// Streams per peer and direction; the receiver also respects the peer's own limit.
    std::uint8_t max_streams = 4;
    /// Content requests a receiver keeps outstanding on one stream.
    std::size_t pipelined_requests = 8;
    /// A sender reads this much per step of file work, and reads again once less than
    /// `send_low_water` waits in the stream's queue.
    std::size_t read_batch = std::size_t{1} * 1024 * 1024;
    std::size_t send_low_water = std::size_t{1} * 1024 * 1024;
    /// A receiver stops reading a stream while this much waits to be written to disk, and
    /// resumes below `write_low_water`.
    std::size_t write_high_water = std::size_t{8} * 1024 * 1024;
    std::size_t write_low_water = std::size_t{2} * 1024 * 1024;
    /// Free space left on the volume after a transfer, beyond the transfer's own size.
    std::uint64_t space_margin = std::uint64_t{16} * 1024 * 1024;
    Microseconds progress_interval{100'000};
    std::size_t max_entries = kMaxTreeEntries;
};

struct TransferProgress {
    TransferId id = 0;
    std::uint64_t bytes_done = 0;
    std::uint64_t bytes_total = 0;
    std::size_t files_done = 0;
    std::size_t files_total = 0;
};

struct TransferOutcome {
    TransferId id = 0;
    std::optional<Error> error; ///< empty: everything arrived
    /// The item's top-level files and folders as created; empty after an error, when nothing is
    /// left behind.
    std::vector<std::filesystem::path> items;
    std::size_t skipped_links = 0; ///< links not recreated (outside the item, or not permitted)
};

struct ContentOutcome {
    TransferId id = 0;
    std::optional<Error> error;
    std::vector<std::uint8_t> content; ///< the whole content; empty after an error
};

/// Where sending an item to one device stands.
enum class SendState : std::uint8_t {
    Sending = 1,
    Done = 2,    ///< every byte of its files went out
    Stopped = 3, ///< the device stopped asking, or the item was withdrawn
};

/// Sending an offered item to one device, as its files' content goes out.
struct SendProgress {
    mesh::DeviceId device{};
    protocol::ObjectRef item;
    std::uint64_t bytes_sent = 0;
    std::uint64_t bytes_total = 0;
    SendState state = SendState::Sending;
    bool operator==(const SendProgress&) const = default;
};

struct OfferReady {
    protocol::ObjectRef item;
    std::optional<Error> error;
    std::uint64_t total_bytes = 0;
    std::size_t entry_count = 0;
    std::size_t skipped = 0; ///< entries that cannot travel
    std::string name;        ///< the first root as it travels (NFC); empty after an error
    bool folder = false;     ///< the first root is a folder
};

class TransferObserver {
public:
    virtual ~TransferObserver() = default;
    /// An offered item was scanned: its size, or why it cannot be sent.
    virtual void on_offer_ready(const OfferReady& ready) = 0;
    virtual void on_transfer_progress(const TransferProgress& progress) = 0;
    /// The end of a receive(), once nothing is being written any more.
    virtual void on_transfer_finished(const TransferOutcome& outcome) = 0;
    /// The end of a fetch_content().
    virtual void on_content_received(const ContentOutcome& outcome) = 0;
    /// About once per progress interval while a device fetches an item's files, and at the end.
    virtual void on_send_progress(const SendProgress& progress) = 0;
};

/// Where file work runs. Both run their work in the order posted.
class TransferExecutor {
public:
    virtual ~TransferExecutor() = default;
    /// Off the mesh's thread: reading, hashing, writing and scanning files.
    virtual void run_file_work(std::function<void()> work) = 0;
    /// Back on the mesh's thread, outside any mesh callback; flushes the mesh afterwards.
    virtual void run_on_mesh(std::function<void(TimePoint)> work) = 0;
};

} // namespace osdrop::transfer
