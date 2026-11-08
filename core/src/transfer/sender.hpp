#pragma once

// The sending side of transfers: items this computer offers, and the streams peers open to fetch
// them. Requests on a stream are served in order. Manifests come from one scan per item; file
// content is read and hashed on the file thread a batch at a time, and the next batch is read only
// once the stream's queue has room, so a slow receiver holds the reads back. ContentEnd carries
// the digest of the whole entry; a file that changed since the scan fails with IO_ERROR.

#include "crypto/primitives.hpp"
#include "transfer/manifest.hpp"
#include "transfer/streams.hpp"
#include "transfer/types.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace osdrop::transfer {

class Sender {
public:
    Sender(const TransferTunables& tunables, Streams& streams, TransferExecutor& executor,
           TransferObserver& observer);
    ~Sender();
    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;
    Sender(Sender&&) = delete;
    Sender& operator=(Sender&&) = delete;

    /// Offers local files and folders (absolute paths) as one item of an object, to the devices
    /// in `audience`; scanning starts at once and on_offer_ready follows. Replaces an earlier
    /// offer of the same item.
    [[nodiscard]] Status<Error> offer(const protocol::ObjectRef& item,
                                      std::vector<std::filesystem::path> roots,
                                      std::set<mesh::DeviceId> audience);
    /// Offers one single content held in memory (clipboard formats 1-4), to the devices in
    /// `audience`. Replaces an earlier offer of the same item.
    [[nodiscard]] Status<Error>
    offer_content(const protocol::ObjectRef& item,
                  std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                  std::set<mesh::DeviceId> audience);
    /// Withdraws every item of an object; requests for them fail with CANCELLED.
    void withdraw(protocol::ObjectKind kind, std::uint64_t object_id);

    // --- Streams peers opened ---
    [[nodiscard]] bool serves(const StreamKey& key) const { return serving_.contains(key); }
    void opened(const StreamKey& key);
    void message(const StreamKey& key, const protocol::TransferMessage& message);
    void pumped(const StreamKey& key, std::size_t queued);
    void closed(const StreamKey& key);

    struct ReadState;
    struct Batch;
    struct DigestCache;

private:
    struct Offer {
        std::vector<std::filesystem::path> roots;
        std::set<mesh::DeviceId> audience;
        std::shared_ptr<const SourceTree> tree;
        std::optional<Error> scan_error;
        std::uint64_t generation = 0; ///< tells a stale scan result from the current one
        std::shared_ptr<DigestCache> digests;
        std::shared_ptr<const std::vector<std::uint8_t>> content; ///< single contents only
        crypto::ContentHasher::Digest content_digest{};
    };

    struct Job {
        std::uint32_t request_id = 0;
        protocol::ObjectRef item;
        bool manifest = false;
        std::uint32_t entry = 0;
        std::uint64_t offset = 0;
        std::uint64_t length = 0;
    };

    struct Serving {
        std::deque<Job> jobs;
        std::uint32_t reading = 0; ///< the request a batch is being read for
        std::shared_ptr<ReadState> read;
        std::uint64_t sent = 0; ///< content bytes sent for the front job
    };

    void scan(const protocol::ObjectRef& item, Offer& offer);
    void scanned(const protocol::ObjectRef& item, std::uint64_t generation,
                 std::shared_ptr<const SourceTree> tree, std::optional<Error> error);
    void advance(const StreamKey& key);
    /// Starts or continues the front job; false when it waits (scan, read, queue).
    bool serve_front(const StreamKey& key, Serving& serving);
    bool serve_content(const StreamKey& key, Serving& serving, const Job& job, Offer& offer);
    /// A single content from memory, as much as the stream's queue takes.
    bool serve_memory(const StreamKey& key, Serving& serving, const Job& job, const Offer& offer);
    void read_next(const StreamKey& key, Serving& serving, std::uint32_t request_id);
    void read_done(const StreamKey& key, std::uint32_t request_id, Batch batch);
    void finish_job(const StreamKey& key, Serving& serving, std::optional<Error> error);
    /// Removes the jobs `matches` selects, each answered with `error` (none: dropped silently).
    void drop_jobs(const std::function<bool(const StreamKey&, const Job&)>& matches,
                   std::optional<Error> error);
    void fail(const StreamKey& key, std::uint32_t request_id, Error error);

    const TransferTunables& tunables_;
    Streams& streams_;
    TransferExecutor& executor_;
    TransferObserver& observer_;
    std::map<protocol::ObjectRef, Offer, ObjectOrder> offers_;
    std::map<StreamKey, Serving> serving_;
    std::uint64_t next_generation_ = 1;
};

} // namespace osdrop::transfer
