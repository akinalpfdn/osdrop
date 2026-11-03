#include "transfer/sender.hpp"

#include "crypto/primitives.hpp"
#include "protocol/constants.hpp"
#include "protocol/wire.hpp"
#include "transfer/files.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <utility>
#include <variant>

namespace osdrop::transfer {

/// Digests of whole entries computed so far; on the file thread only.
struct Sender::DigestCache {
    std::map<std::uint32_t, crypto::ContentHasher::Digest> digests;
};

/// The read of one request, on the file thread only.
struct Sender::ReadState {
    std::filesystem::path local;
    std::uint32_t entry = 0;
    std::uint64_t size = 0; ///< as scanned
    std::int64_t modified_us = 0;
    std::uint64_t position = 0; ///< the next byte to send
    std::uint64_t end = 0;      ///< one past the last byte to send
    std::shared_ptr<DigestCache> digests;
    std::optional<FileReader> reader;
    std::optional<crypto::ContentHasher::Digest> known; ///< from the cache
    crypto::ContentHasher hasher;
    std::uint64_t hashed = 0;
};

struct Sender::Batch {
    std::uint64_t offset = 0; ///< of the first chunk
    std::vector<std::vector<std::uint8_t>> chunks;
    bool done = false;
    std::optional<Error> error;
    crypto::ContentHasher::Digest digest{};
};

namespace {

constexpr std::size_t kHashBuffer = std::size_t{1} * 1024 * 1024;

/// The file, opened on first use; a digest computed before is looked up then.
Expected<FileReader*, Error> open_once(Sender::ReadState& state) {
    if (!state.reader) {
        auto reader = FileReader::open(state.local);
        if (!reader) {
            return Expected<FileReader*, Error>::failure(reader.error());
        }
        state.reader.emplace(std::move(reader.value()));
        if (const auto cached = state.digests->digests.find(state.entry);
            cached != state.digests->digests.end()) {
            state.known = cached->second;
        }
    }
    return &*state.reader;
}

/// Hashes the file from where hashing stopped up to `until`.
Status<Error> hash_through(Sender::ReadState& state, FileReader& reader, std::uint64_t until) {
    if (state.hashed >= until) {
        return Status<Error>::ok();
    }
    std::vector<std::uint8_t> buffer(
        static_cast<std::size_t>(std::min<std::uint64_t>(kHashBuffer, until - state.hashed)));
    while (state.hashed < until) {
        const auto want =
            static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), until - state.hashed));
        const auto got = reader.read(state.hashed, std::span(buffer).first(want));
        if (!got || got.value() == 0) {
            return Status<Error>::failure(got ? Error::IoError : got.error()); // it shrank
        }
        state.hasher.update(std::span(buffer).first(got.value()));
        state.hashed += got.value();
    }
    return Status<Error>::ok();
}

/// Up to `budget` bytes of the range, as chunks, hashed unless the digest is known.
Status<Error> read_range(Sender::ReadState& state, FileReader& reader, std::size_t budget,
                         std::vector<std::vector<std::uint8_t>>& chunks) {
    while (state.position < state.end && budget > 0) {
        const auto want = static_cast<std::size_t>(
            std::min<std::uint64_t>({protocol::kMaxChunkData, state.end - state.position, budget}));
        std::vector<std::uint8_t> data(want);
        const auto got = reader.read(state.position, data);
        if (!got || got.value() == 0) {
            return Status<Error>::failure(got ? Error::IoError : got.error());
        }
        data.resize(got.value());
        if (!state.known) {
            state.hasher.update(data);
            state.hashed += data.size();
        }
        state.position += data.size();
        budget -= std::min(budget, data.size());
        chunks.push_back(std::move(data));
    }
    return Status<Error>::ok();
}

/// The digest of the whole entry once its range is sent. IO_ERROR when the file is not as
/// scanned any more (size or time).
Expected<crypto::ContentHasher::Digest, Error> whole_digest(Sender::ReadState& state,
                                                            FileReader& reader) {
    using Result = Expected<crypto::ContentHasher::Digest, Error>;
    if (!state.known) {
        if (const auto rest = hash_through(state, reader, state.size); !rest) {
            return Result::failure(rest.error());
        }
        std::array<std::uint8_t, 1> beyond{};
        const auto more = reader.read(state.size, beyond);
        if (!more || more.value() != 0) { // it grew since the scan
            return Result::failure(more ? Error::IoError : more.error());
        }
        state.known = state.hasher.finish();
        state.digests->digests[state.entry] = *state.known;
    }
    if (modified_us(state.local) != state.modified_us) {
        return Result::failure(Error::IoError); // changed since the scan
    }
    return *state.known;
}

/// Reads up to `budget` bytes of the range; at its end, the digest of the whole entry.
Sender::Batch read_batch(Sender::ReadState& state, std::size_t budget) {
    Sender::Batch batch;
    batch.offset = state.position;
    const auto reader = open_once(state);
    if (!reader) {
        batch.error = reader.error();
        return batch;
    }
    if (!state.known) {
        if (const auto prefix = hash_through(state, *reader.value(), state.position); !prefix) {
            batch.error = prefix.error();
            return batch;
        }
    }
    if (const auto read = read_range(state, *reader.value(), budget, batch.chunks); !read) {
        batch.error = read.error();
        return batch;
    }
    if (state.position < state.end) {
        return batch;
    }
    const auto digest = whole_digest(state, *reader.value());
    if (!digest) {
        batch.error = digest.error();
        return batch;
    }
    batch.done = true;
    batch.digest = digest.value();
    return batch;
}

} // namespace

Sender::Sender(const TransferTunables& tunables, Streams& streams, TransferExecutor& executor,
               TransferObserver& observer)
    : tunables_(tunables), streams_(streams), executor_(executor), observer_(observer) {}

Sender::~Sender() = default;

Status<Error> Sender::offer(const protocol::ObjectRef& item,
                            std::vector<std::filesystem::path> roots,
                            std::set<mesh::DeviceId> audience) {
    if (roots.empty() || !std::ranges::all_of(roots, [](const std::filesystem::path& root) {
            return root.is_absolute();
        })) {
        return Status<Error>::failure(Error::InvalidArgument);
    }
    drop_jobs([&](const StreamKey& /*key*/, const Job& job) { return job.item == item; },
              Error::Cancelled);
    Offer& offer = offers_[item];
    offer = Offer{.roots = std::move(roots),
                  .audience = std::move(audience),
                  .tree = nullptr,
                  .scan_error = std::nullopt,
                  .generation = next_generation_++,
                  .digests = std::make_shared<DigestCache>()};
    scan(item, offer);
    return Status<Error>::ok();
}

void Sender::withdraw(protocol::ObjectKind kind, std::uint64_t object_id) {
    const auto matches = [&](const protocol::ObjectRef& item) {
        return item.kind == kind && item.object_id == object_id;
    };
    std::erase_if(offers_, [&](const auto& entry) { return matches(entry.first); });
    drop_jobs([&](const StreamKey& /*key*/, const Job& job) { return matches(job.item); },
              Error::Cancelled);
    std::vector<StreamKey> keys;
    keys.reserve(serving_.size());
    for (const auto& [key, serving] : serving_) {
        keys.push_back(key);
    }
    for (const StreamKey& key : keys) {
        advance(key);
    }
}

void Sender::opened(const StreamKey& key) {
    streams_.accepted(key);
    const auto from_device = std::ranges::count_if(
        serving_, [&](const auto& entry) { return entry.first.device == key.device; });
    if (std::cmp_greater_equal(from_device, tunables_.max_streams)) {
        fail(key, 0, Error::PeerBusy);
        streams_.close(key, true);
        return;
    }
    serving_.try_emplace(key);
}

void Sender::message(const StreamKey& key, const protocol::TransferMessage& message) {
    const auto it = serving_.find(key);
    if (it == serving_.end()) {
        return;
    }
    Serving& serving = it->second;
    if (const auto* request = std::get_if<protocol::ManifestRequest>(&message)) {
        serving.jobs.push_back(
            {.request_id = request->request_id, .item = request->object, .manifest = true});
    } else if (const auto* content = std::get_if<protocol::ContentRequest>(&message)) {
        serving.jobs.push_back({.request_id = content->request_id,
                                .item = content->object,
                                .manifest = false,
                                .entry = content->entry_index,
                                .offset = content->offset,
                                .length = content->length});
    } else if (const auto* cancel = std::get_if<protocol::TransferCancel>(&message)) {
        drop_jobs([&](const StreamKey& at,
                      const Job& job) { return at == key && job.request_id == cancel->request_id; },
                  std::nullopt);
    } else if (std::holds_alternative<protocol::TransferError>(message)) {
        serving_.erase(it); // the receiver gave up on the stream
        streams_.close(key, false);
        return;
    } else {
        serving_.erase(it);
        fail(key, 0, Error::UnexpectedMessage);
        streams_.close(key, true);
        return;
    }
    advance(key);
}

void Sender::pumped(const StreamKey& key, std::size_t queued) {
    if (queued < tunables_.send_low_water) {
        advance(key);
    }
}

void Sender::closed(const StreamKey& key) {
    serving_.erase(key);
}

void Sender::scan(const protocol::ObjectRef& item, Offer& offer) {
    executor_.run_file_work([this, item, generation = offer.generation, roots = offer.roots,
                             max_entries = tunables_.max_entries] {
        auto result = transfer::scan(roots, max_entries);
        std::shared_ptr<const SourceTree> tree;
        std::optional<Error> error;
        if (result) {
            tree = std::make_shared<const SourceTree>(std::move(result.value()));
        } else {
            error = result.error();
        }
        executor_.run_on_mesh([this, item, generation, tree, error](TimePoint /*now*/) {
            scanned(item, generation, tree, error);
        });
    });
}

void Sender::scanned(const protocol::ObjectRef& item, std::uint64_t generation,
                     std::shared_ptr<const SourceTree> tree, std::optional<Error> error) {
    const auto it = offers_.find(item);
    if (it == offers_.end() || it->second.generation != generation) {
        return; // withdrawn or replaced meanwhile
    }
    Offer& offer = it->second;
    OfferReady ready{
        .item = item, .error = std::nullopt, .total_bytes = 0, .entry_count = 0, .skipped = 0};
    if (error) {
        offer.scan_error = error;
    } else if (tree->entries.empty()) {
        offer.scan_error = Error::NotFound; // nothing in it can travel
        ready.skipped = tree->skipped;
    } else {
        ready.total_bytes = tree->total_bytes;
        ready.entry_count = tree->entries.size();
        ready.skipped = tree->skipped;
        offer.tree = std::move(tree);
    }
    ready.error = offer.scan_error;
    observer_.on_offer_ready(ready);
    std::vector<StreamKey> keys;
    keys.reserve(serving_.size());
    for (const auto& [key, serving] : serving_) {
        keys.push_back(key);
    }
    for (const StreamKey& key : keys) {
        advance(key);
    }
}

void Sender::advance(const StreamKey& key) {
    const auto it = serving_.find(key);
    if (it == serving_.end()) {
        return;
    }
    while (serve_front(key, it->second)) {
    }
}

bool Sender::serve_front(const StreamKey& key, Serving& serving) {
    if (serving.jobs.empty() || serving.reading != 0) {
        return false;
    }
    const Job job = serving.jobs.front();
    const auto offer = offers_.find(job.item);
    if (offer == offers_.end()) {
        finish_job(key, serving, Error::NotFound);
        return true;
    }
    if (!offer->second.audience.contains(key.device)) {
        finish_job(key, serving, Error::NotPermitted);
        return true;
    }
    if (offer->second.scan_error) {
        finish_job(key, serving, offer->second.scan_error);
        return true;
    }
    if (!offer->second.tree) {
        return false; // scanned() continues
    }
    if (job.manifest) {
        for (protocol::Manifest& part :
             manifest_messages(job.request_id, offer->second.tree->entries)) {
            streams_.send(key, std::move(part));
        }
        finish_job(key, serving, std::nullopt);
        return true;
    }
    return serve_content(key, serving, job, offer->second);
}

bool Sender::serve_content(const StreamKey& key, Serving& serving, const Job& job, Offer& offer) {
    const SourceTree& tree = *offer.tree;
    if (job.entry >= tree.entries.size() ||
        tree.entries[job.entry].kind == protocol::EntryKind::Folder ||
        job.offset > tree.entries[job.entry].byte_count) {
        finish_job(key, serving, Error::InvalidArgument);
        return true;
    }
    const protocol::ManifestEntry& entry = tree.entries[job.entry];
    const std::uint64_t end = job.offset + std::min(job.length, entry.byte_count - job.offset);
    if (entry.kind == protocol::EntryKind::Link) {
        const std::string& target = tree.sources[job.entry].link_target;
        const auto bytes = protocol::bytes_of(target);
        const auto range = std::span(bytes).subspan(job.offset, end - job.offset);
        if (!range.empty()) {
            streams_.send(key, protocol::ContentChunk{.request_id = job.request_id,
                                                      .offset = job.offset,
                                                      .data = {range.begin(), range.end()}});
        }
        crypto::ContentHasher hasher;
        hasher.update(bytes);
        streams_.send(key, protocol::ContentEnd{.request_id = job.request_id,
                                                .byte_count = range.size(),
                                                .digest = hasher.finish()});
        finish_job(key, serving, std::nullopt);
        return true;
    }
    if (!serving.read) {
        serving.read = std::make_shared<ReadState>();
        serving.read->local = tree.sources[job.entry].local;
        serving.read->entry = job.entry;
        serving.read->size = entry.byte_count;
        serving.read->modified_us = entry.modified_us;
        serving.read->position = job.offset;
        serving.read->end = end;
        serving.read->digests = offer.digests;
    }
    if (streams_.queued_bytes(key) < tunables_.send_low_water) {
        read_next(key, serving, job.request_id);
    }
    return false; // read_done() or pumped() continues
}

void Sender::read_next(const StreamKey& key, Serving& serving, std::uint32_t request_id) {
    serving.reading = request_id;
    executor_.run_file_work(
        [this, key, request_id, read = serving.read, budget = tunables_.read_batch] {
            Batch result = read_batch(*read, budget);
            executor_.run_on_mesh([this, key, request_id, batch = std::move(result)](TimePoint) {
                read_done(key, request_id, batch);
            });
        });
}

void Sender::read_done(const StreamKey& key, std::uint32_t request_id, Batch batch) {
    const auto it = serving_.find(key);
    if (it == serving_.end()) {
        return; // the stream closed meanwhile
    }
    Serving& serving = it->second;
    if (serving.reading == request_id) {
        serving.reading = 0;
    }
    if (serving.jobs.empty() || serving.jobs.front().request_id != request_id) {
        advance(key); // cancelled meanwhile
        return;
    }
    if (batch.error) {
        finish_job(key, serving, batch.error);
        advance(key);
        return;
    }
    std::uint64_t offset = batch.offset;
    for (std::vector<std::uint8_t>& data : batch.chunks) {
        const std::size_t size = data.size();
        streams_.send(key, protocol::ContentChunk{.request_id = request_id,
                                                  .offset = offset,
                                                  .data = std::move(data)});
        offset += size;
        serving.sent += size;
    }
    if (batch.done) {
        streams_.send(key, protocol::ContentEnd{.request_id = request_id,
                                                .byte_count = serving.sent,
                                                .digest = batch.digest});
        finish_job(key, serving, std::nullopt);
    }
    advance(key);
}

void Sender::finish_job(const StreamKey& key, Serving& serving, std::optional<Error> error) {
    if (error) {
        fail(key, serving.jobs.front().request_id, *error);
    }
    serving.jobs.pop_front();
    serving.read.reset();
    serving.sent = 0;
}

void Sender::drop_jobs(const std::function<bool(const StreamKey&, const Job&)>& matches,
                       std::optional<Error> error) {
    for (auto& [key, serving] : serving_) {
        std::deque<Job> kept;
        bool front = true;
        for (const Job& job : serving.jobs) {
            if (!matches(key, job)) {
                kept.push_back(job);
            } else {
                streams_.drop(key, job.request_id);
                if (error) {
                    fail(key, job.request_id, *error);
                }
                if (front) {
                    serving.read.reset();
                    serving.sent = 0;
                }
            }
            front = false;
        }
        serving.jobs = std::move(kept);
    }
}

void Sender::fail(const StreamKey& key, std::uint32_t request_id, Error error) {
    streams_.send(key, protocol::TransferError{.request_id = request_id,
                                               .code = static_cast<std::uint16_t>(error)});
}

} // namespace osdrop::transfer
