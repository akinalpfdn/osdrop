#pragma once

// The receiving side of transfers: receive() fetches one item from a peer into a folder. Each
// transfer opens its own stream (at most the lower of both sides' stream limits per peer; more
// wait), asks for the manifest, checks it and maps its names, checks free space, creates the
// folders, then asks for every file and link with a few requests in flight. Data is written and
// verified on the file thread; while too much waits to be written the stream is paused. Links are
// recreated at the end when they stay inside their item. A failure or cancellation removes
// everything the transfer created before it is reported.

#include "transfer/manifest.hpp"
#include "transfer/streams.hpp"
#include "transfer/types.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace osdrop::transfer {

class Receiver {
public:
    Receiver(const TransferTunables& tunables, Streams& streams, TransferExecutor& executor,
             TransferObserver& observer);
    ~Receiver();
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;
    Receiver(Receiver&&) = delete;
    Receiver& operator=(Receiver&&) = delete;

    /// Fetches `item` from `device` into the folder `destination` (absolute). NOT_PERMITTED when
    /// the device may not send this computer files, INVALID_STATE when it is not connected.
    [[nodiscard]] Expected<TransferId, Error> receive(const mesh::DeviceId& device,
                                                      const protocol::ObjectRef& item,
                                                      const std::filesystem::path& destination);
    /// Ends a transfer with CANCELLED; what it created is removed.
    void cancel(TransferId id);
    /// The devices that may send this computer files (none until set). Transfers from others
    /// end with NOT_PERMITTED.
    void set_files_permitted(std::set<mesh::DeviceId> devices);
    /// Quitting: every transfer is rolled back without a report.
    void shutdown();

    void peer_connected(const mesh::DeviceId& device, std::uint8_t max_streams);
    void peer_disconnected(const mesh::DeviceId& device);

    // --- Streams this side opened ---
    [[nodiscard]] bool owns(const StreamKey& key) const { return by_stream_.contains(key); }
    void writable(const StreamKey& key);
    void message(const StreamKey& key, const protocol::TransferMessage& message);
    void closed(const StreamKey& key, std::optional<Error> reason);

    struct Disk;

private:
    enum class Stage : std::uint8_t {
        Waiting,   ///< for a free stream
        Opening,   ///< the stream connects
        Listing,   ///< the manifest arrives
        Planning,  ///< free space, folders
        Fetching,  ///< contents
        Finishing, ///< links, folder times
        Failing,   ///< rolling back
    };

    struct Pending {
        std::size_t entry = 0;
        std::uint64_t received = 0;
        std::string link; ///< a link's target as it arrives
    };

    struct Fetch {
        Fetch(TransferId fetch_id, const mesh::DeviceId& from, const protocol::ObjectRef& object,
              std::filesystem::path folder, std::size_t max_entries)
            : id(fetch_id), device(from), item(object), destination(std::move(folder)),
              collector(current_platform(), max_entries) {}

        TransferId id;
        mesh::DeviceId device;
        protocol::ObjectRef item;
        std::filesystem::path destination;
        Stage stage = Stage::Waiting;
        std::optional<Error> error;
        std::uint32_t stream = 0;
        std::uint32_t next_request = 1;
        std::uint32_t manifest_request = 0;
        ManifestCollector collector;
        std::shared_ptr<const ReceivePlan> plan;
        std::size_t next_entry = 0;
        std::map<std::uint32_t, Pending> pending;
        std::size_t finishing = 0; ///< files being verified on the file thread
        std::vector<std::pair<std::size_t, std::string>> links;
        std::uint64_t bytes_done = 0;
        std::uint64_t unwritten = 0;
        std::size_t files_done = 0;
        bool paused = false;
        std::optional<TimePoint> last_progress;
        std::shared_ptr<Disk> disk;
    };

    [[nodiscard]] static StreamKey key_of(const Fetch& fetch) {
        return {.device = fetch.device, .id = fetch.stream};
    }
    void start_waiting(const mesh::DeviceId& device);
    void opened(TransferId id, const mesh::DeviceId& device, Expected<std::uint32_t, Error> stream);
    void received_manifest(Fetch& fetch, const protocol::Manifest& part);
    void received_chunk(Fetch& fetch, const protocol::ContentChunk& chunk);
    void received_end(Fetch& fetch, const protocol::ContentEnd& end);
    void plan(Fetch& fetch);
    void planned(TransferId id, Status<Error> status, TimePoint now);
    void request_more(Fetch& fetch);
    void written(TransferId id, std::uint64_t bytes, Status<Error> status, TimePoint now);
    void finished(TransferId id, Status<Error> status, TimePoint now);
    void maybe_finalize(Fetch& fetch);
    void finalized(TransferId id, std::size_t skipped_links,
                   std::vector<std::filesystem::path> items, TimePoint now);
    void fail(Fetch& fetch, Error error);
    void rolled_back(TransferId id);
    void report_progress(Fetch& fetch, TimePoint now, bool force);

    const TransferTunables& tunables_;
    Streams& streams_;
    TransferExecutor& executor_;
    TransferObserver& observer_;
    std::map<TransferId, Fetch> fetches_;
    std::map<StreamKey, TransferId> by_stream_;
    std::map<mesh::DeviceId, std::uint8_t> peer_limits_; ///< connected peers' stream limits
    std::set<mesh::DeviceId> permitted_;
    TransferId next_id_ = 1;
};

} // namespace osdrop::transfer
