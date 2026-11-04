#include "transfer/receiver.hpp"

#include "crypto/primitives.hpp"
#include "protocol/wire.hpp"
#include "transfer/files.hpp"

#include <algorithm>
#include <ranges>
#include <span>
#include <system_error>
#include <variant>

namespace osdrop::transfer {

/// What one transfer has on disk; on the file thread only.
struct Receiver::Disk {
    std::filesystem::path destination;
    std::string token;                        ///< names temporaries
    std::vector<std::filesystem::path> paths; ///< per entry; files may end up numbered
    std::map<std::size_t, FileWriter> writers;
    std::vector<std::filesystem::path> created; ///< in creation order
    std::vector<std::filesystem::path> items;   ///< top-level, as created
};

namespace {

namespace fs = std::filesystem;
using protocol::EntryKind;

bool top_level(const PlannedEntry& entry) {
    return entry.target.find('/') == std::string::npos;
}

/// Free space, then every folder, top-level ones numbered when their name is taken.
Status<Error> plan_disk(Receiver::Disk& disk, const ReceivePlan& plan, std::uint64_t margin) {
    std::error_code error;
    if (!fs::is_directory(native_path(disk.destination), error)) {
        return Status<Error>::failure(Error::NotFound);
    }
    if (const auto space = free_space(disk.destination);
        space && (plan.total_bytes > *space || *space - plan.total_bytes < margin)) {
        return Status<Error>::failure(Error::NoSpace);
    }
    std::map<std::string, fs::path> tops; // mapped top-level folder → created folder
    disk.paths.resize(plan.entries.size());
    for (std::size_t i = 0; i < plan.entries.size(); ++i) {
        const PlannedEntry& planned = plan.entries[i];
        const bool folder = planned.entry.kind == EntryKind::Folder;
        const std::size_t slash = planned.target.find('/');
        if (slash == std::string::npos) {
            if (!folder) {
                disk.paths[i] = disk.destination / path_from_utf8(planned.target);
                continue;
            }
            auto made = create_unique_folder(disk.destination, planned.target);
            if (!made) {
                return Status<Error>::failure(made.error());
            }
            disk.created.push_back(made.value());
            disk.items.push_back(made.value());
            tops[planned.target] = made.value();
            disk.paths[i] = std::move(made.value());
            continue;
        }
        disk.paths[i] = tops.at(planned.target.substr(0, slash)) /
                        path_from_utf8(planned.target.substr(slash + 1));
        if (folder) {
            if (!fs::create_directory(native_path(disk.paths[i]), error)) {
                return Status<Error>::failure(
                    error == std::errc::no_space_on_device ? Error::NoSpace : Error::IoError);
            }
            disk.created.push_back(disk.paths[i]);
        }
    }
    return Status<Error>::ok();
}

Status<Error> write_entry(Receiver::Disk& disk, std::size_t entry,
                          std::span<const std::uint8_t> data) {
    auto writer = disk.writers.find(entry);
    if (writer == disk.writers.end()) {
        auto created =
            FileWriter::create(disk.paths.at(entry), disk.token + "-" + std::to_string(entry));
        if (!created) {
            return Status<Error>::failure(created.error());
        }
        writer = disk.writers.emplace(entry, std::move(created.value())).first;
    }
    return writer->second.write(data);
}

Status<Error> finish_entry(Receiver::Disk& disk, std::size_t entry,
                           const protocol::ManifestEntry& manifest,
                           const crypto::ContentHasher::Digest& digest, bool is_top_level) {
    if (const auto opened = write_entry(disk, entry, {}); !opened) { // an empty file has no data
        return opened;
    }
    auto writer = disk.writers.extract(entry);
    const auto placed = writer.mapped().finish(manifest.byte_count, digest, manifest.modified_us,
                                               manifest.executable);
    if (!placed) {
        return Status<Error>::failure(placed.error());
    }
    disk.created.push_back(placed.value());
    if (is_top_level) {
        disk.items.push_back(placed.value());
    }
    return Status<Error>::ok();
}

/// Links that stay inside their item, then the folders' times; the links not recreated.
std::size_t finalize_disk(Receiver::Disk& disk, const ReceivePlan& plan,
                          const std::vector<std::pair<std::size_t, std::string>>& links) {
    std::size_t skipped = 0;
    for (const auto& [entry, target] : links) {
        const auto mapped = mapped_link_target(plan, entry, target);
        if (!mapped) {
            ++skipped;
            continue;
        }
        const fs::path& link = disk.paths.at(entry);
        std::error_code error;
        const bool to_folder =
            fs::is_directory(native_path(link.parent_path() / path_from_utf8(*mapped)), error);
        if (!create_link(link, *mapped, to_folder)) {
            ++skipped; // e.g. Windows without the right to create links
            continue;
        }
        disk.created.push_back(link);
    }
    for (std::size_t i = plan.entries.size(); i-- > 0;) {
        if (plan.entries[i].entry.kind == EntryKind::Folder) {
            set_modified_us(disk.paths[i], plan.entries[i].entry.modified_us);
        }
    }
    return skipped;
}

void rollback(Receiver::Disk& disk) {
    disk.writers.clear(); // removes the temporaries
    for (const fs::path& path : std::views::reverse(disk.created)) {
        std::error_code ignored;
        fs::remove(native_path(path), ignored); // a folder the user added to stays
    }
    disk.created.clear();
    disk.items.clear();
}

Error error_from_wire(std::uint16_t code) {
    return code >= 1 && code <= static_cast<std::uint16_t>(kLastError) ? static_cast<Error>(code)
                                                                       : Error::PeerError;
}

} // namespace

Receiver::Receiver(const TransferTunables& tunables, Streams& streams, TransferExecutor& executor,
                   TransferObserver& observer)
    : tunables_(tunables), streams_(streams), executor_(executor), observer_(observer) {}

Receiver::~Receiver() = default;

Expected<TransferId, Error> Receiver::receive(const mesh::DeviceId& device,
                                              const protocol::ObjectRef& item,
                                              const std::filesystem::path& destination,
                                              std::uint64_t max_bytes) {
    using Result = Expected<TransferId, Error>;
    if (!destination.is_absolute()) {
        return Result::failure(Error::InvalidArgument);
    }
    if (!permitted_.contains(device)) {
        return Result::failure(Error::NotPermitted);
    }
    if (!peer_limits_.contains(device)) {
        return Result::failure(Error::InvalidState);
    }
    const TransferId id = next_id_++;
    fetches_.try_emplace(id, id, device, item, destination, tunables_.max_entries, max_bytes);
    start_waiting(device);
    return id;
}

Expected<TransferId, Error> Receiver::fetch_content(const mesh::DeviceId& device,
                                                    const protocol::ObjectRef& item,
                                                    std::uint64_t max_bytes) {
    using Result = Expected<TransferId, Error>;
    if (item.kind != protocol::ObjectKind::Clipboard ||
        item.item_index == protocol::kFilesClipboardFormat) {
        return Result::failure(Error::InvalidArgument);
    }
    if (!peer_limits_.contains(device)) {
        return Result::failure(Error::InvalidState);
    }
    const TransferId id = next_id_++;
    auto [it, added] =
        fetches_.try_emplace(id, id, device, item, std::filesystem::path{}, 1, max_bytes);
    it->second.content = true;
    start_waiting(device);
    return id;
}

void Receiver::cancel(TransferId id) {
    if (const auto it = fetches_.find(id); it != fetches_.end()) {
        fail(it->second, Error::Cancelled);
    }
}

void Receiver::set_files_permitted(std::set<mesh::DeviceId> devices) {
    permitted_ = std::move(devices);
    for (auto& [id, fetch] : fetches_) {
        if (!permitted_.contains(fetch.device)) {
            fail(fetch, Error::NotPermitted);
        }
    }
}

void Receiver::shutdown() {
    for (auto& [id, fetch] : fetches_) {
        if (fetch.stage != Stage::Failing) {
            fail(fetch, Error::Cancelled);
        }
    }
    fetches_.clear(); // rolled_back() finds nothing to report
}

void Receiver::peer_connected(const mesh::DeviceId& device, std::uint8_t max_streams) {
    peer_limits_[device] = max_streams;
    start_waiting(device);
}

void Receiver::peer_disconnected(const mesh::DeviceId& device) {
    peer_limits_.erase(device);
    for (auto& [id, fetch] : fetches_) {
        if (fetch.device == device) {
            fail(fetch, Error::ConnectionClosed);
        }
    }
}

void Receiver::writable(const StreamKey& key) {
    const auto it = by_stream_.find(key);
    if (it == by_stream_.end()) {
        return;
    }
    Fetch& fetch = fetches_.at(it->second);
    if (fetch.stage != Stage::Opening) {
        return;
    }
    if (fetch.content) {
        fetch.stage = Stage::Fetching;
        const std::uint32_t request = fetch.next_request++;
        fetch.pending.emplace(request, Pending{.entry = 0, .received = 0, .link = {}});
        // One byte past the limit tells a content that is too large.
        const std::uint64_t length =
            fetch.max_bytes == UINT64_MAX ? UINT64_MAX : fetch.max_bytes + 1;
        streams_.send(key, protocol::ContentRequest{.request_id = request,
                                                    .object = fetch.item,
                                                    .entry_index = 0,
                                                    .offset = 0,
                                                    .length = length});
        return;
    }
    fetch.stage = Stage::Listing;
    fetch.manifest_request = fetch.next_request++;
    streams_.send(
        key, protocol::ManifestRequest{.request_id = fetch.manifest_request, .object = fetch.item});
}

void Receiver::message(const StreamKey& key, const protocol::TransferMessage& message) {
    const auto it = by_stream_.find(key);
    if (it == by_stream_.end()) {
        return;
    }
    Fetch& fetch = fetches_.at(it->second);
    if (fetch.content) {
        if (const auto* chunk = std::get_if<protocol::ContentChunk>(&message)) {
            received_content_chunk(fetch, *chunk);
        } else if (const auto* end = std::get_if<protocol::ContentEnd>(&message)) {
            received_content_end(fetch, *end);
        } else if (const auto* error = std::get_if<protocol::TransferError>(&message)) {
            fail(fetch, error_from_wire(error->code));
        } else {
            fail(fetch, Error::UnexpectedMessage);
        }
        return;
    }
    if (const auto* manifest = std::get_if<protocol::Manifest>(&message)) {
        received_manifest(fetch, *manifest);
    } else if (const auto* chunk = std::get_if<protocol::ContentChunk>(&message)) {
        received_chunk(fetch, *chunk);
    } else if (const auto* end = std::get_if<protocol::ContentEnd>(&message)) {
        received_end(fetch, *end);
    } else if (const auto* error = std::get_if<protocol::TransferError>(&message)) {
        fail(fetch, error_from_wire(error->code));
    } else {
        fail(fetch, Error::UnexpectedMessage);
    }
}

void Receiver::closed(const StreamKey& key, std::optional<Error> reason) {
    const auto it = by_stream_.find(key);
    if (it == by_stream_.end()) {
        return;
    }
    Fetch& fetch = fetches_.at(it->second);
    by_stream_.erase(it);
    fetch.stream = 0; // nothing left to close
    fail(fetch, reason.value_or(Error::ConnectionClosed));
}

void Receiver::start_waiting(const mesh::DeviceId& device) {
    const auto limit = peer_limits_.find(device);
    if (limit == peer_limits_.end()) {
        return;
    }
    const std::size_t allowed = std::min(limit->second, tunables_.max_streams);
    auto active = static_cast<std::size_t>(std::ranges::count_if(fetches_, [&](const auto& entry) {
        const Fetch& fetch = entry.second;
        return fetch.device == device && fetch.stage != Stage::Waiting &&
               fetch.stage != Stage::Failing;
    }));
    for (auto& [fetch_id, fetch] : fetches_) {
        if (active >= allowed) {
            break;
        }
        if (fetch.device != device || fetch.stage != Stage::Waiting) {
            continue;
        }
        fetch.stage = Stage::Opening;
        ++active;
        streams_.open(device, [this, id = fetch_id, device](Expected<std::uint32_t, Error> stream) {
            opened(id, device, stream);
        });
    }
}

void Receiver::opened(TransferId id, const mesh::DeviceId& device,
                      Expected<std::uint32_t, Error> stream) {
    const auto it = fetches_.find(id);
    if (it == fetches_.end() || it->second.stage != Stage::Opening) {
        if (stream) { // cancelled meanwhile
            streams_.close({.device = device, .id = stream.value()}, false);
        }
        return;
    }
    if (!stream) {
        fail(it->second, stream.error());
        return;
    }
    it->second.stream = stream.value();
    by_stream_[key_of(it->second)] = id;
}

void Receiver::received_manifest(Fetch& fetch, const protocol::Manifest& part) {
    if (fetch.stage != Stage::Listing || part.request_id != fetch.manifest_request) {
        fail(fetch, Error::UnexpectedMessage);
        return;
    }
    if (const auto added = fetch.collector.add(part); !added) {
        fail(fetch, added.error());
        return;
    }
    if (fetch.collector.complete()) {
        if (fetch.collector.plan().total_bytes > fetch.max_bytes) {
            fail(fetch, Error::TooLarge);
            return;
        }
        plan(fetch);
    }
}

void Receiver::received_content_chunk(Fetch& fetch, const protocol::ContentChunk& chunk) {
    const auto pending = fetch.pending.find(chunk.request_id);
    if (fetch.stage != Stage::Fetching || pending == fetch.pending.end() ||
        chunk.offset != pending->second.received) {
        fail(fetch, Error::UnexpectedMessage);
        return;
    }
    if (chunk.data.size() > fetch.max_bytes - fetch.buffer.size()) {
        fail(fetch, Error::TooLarge);
        return;
    }
    pending->second.received += chunk.data.size();
    fetch.buffer.insert(fetch.buffer.end(), chunk.data.begin(), chunk.data.end());
}

void Receiver::received_content_end(Fetch& fetch, const protocol::ContentEnd& end) {
    const auto pending = fetch.pending.find(end.request_id);
    if (fetch.stage != Stage::Fetching || pending == fetch.pending.end()) {
        fail(fetch, Error::UnexpectedMessage);
        return;
    }
    crypto::ContentHasher hasher;
    hasher.update(fetch.buffer);
    if (end.byte_count != fetch.buffer.size() || hasher.finish() != end.digest) {
        fail(fetch, Error::IntegrityFailed);
        return;
    }
    const TransferId id = fetch.id;
    const mesh::DeviceId device = fetch.device;
    const ContentOutcome outcome{
        .id = id, .error = std::nullopt, .content = std::move(fetch.buffer)};
    finish_stream(fetch);
    fetches_.erase(id);
    observer_.on_content_received(outcome);
    start_waiting(device);
}

void Receiver::finish_stream(Fetch& fetch) {
    if (fetch.stream != 0) {
        by_stream_.erase(key_of(fetch));
        streams_.close(key_of(fetch), false);
        fetch.stream = 0;
    }
}

void Receiver::plan(Fetch& fetch) {
    fetch.stage = Stage::Planning;
    fetch.plan = std::make_shared<const ReceivePlan>(fetch.collector.take());
    fetch.disk = std::make_shared<Disk>();
    fetch.disk->destination = fetch.destination;
    fetch.disk->token = std::to_string(fetch.id);
    executor_.run_file_work([this, id = fetch.id, disk = fetch.disk, plan = fetch.plan,
                             margin = tunables_.space_margin] {
        const Status<Error> status = plan_disk(*disk, *plan, margin);
        executor_.run_on_mesh([this, id, status](TimePoint now) { planned(id, status, now); });
    });
}

void Receiver::planned(TransferId id, Status<Error> status, TimePoint now) {
    const auto it = fetches_.find(id);
    if (it == fetches_.end() || it->second.stage != Stage::Planning) {
        return;
    }
    if (!status) {
        fail(it->second, status.error());
        return;
    }
    it->second.stage = Stage::Fetching;
    report_progress(it->second, now, true);
    request_more(it->second);
}

void Receiver::request_more(Fetch& fetch) {
    const std::vector<PlannedEntry>& entries = fetch.plan->entries;
    while (fetch.pending.size() < tunables_.pipelined_requests &&
           fetch.next_entry < entries.size()) {
        const std::size_t index = fetch.next_entry++;
        const protocol::ManifestEntry& entry = entries[index].entry;
        if (entry.kind == EntryKind::Folder) {
            continue; // created while planning
        }
        const std::uint32_t request = fetch.next_request++;
        fetch.pending.emplace(request, Pending{.entry = index, .received = 0, .link = {}});
        streams_.send(key_of(fetch), protocol::ContentRequest{
                                         .request_id = request,
                                         .object = fetch.item,
                                         .entry_index = static_cast<std::uint32_t>(index),
                                         .offset = 0,
                                         .length = std::max<std::uint64_t>(1, entry.byte_count)});
    }
    maybe_finalize(fetch);
}

void Receiver::received_chunk(Fetch& fetch, const protocol::ContentChunk& chunk) {
    const auto pending = fetch.pending.find(chunk.request_id);
    if (fetch.stage != Stage::Fetching || pending == fetch.pending.end()) {
        fail(fetch, Error::UnexpectedMessage);
        return;
    }
    Pending& state = pending->second;
    const protocol::ManifestEntry& entry = fetch.plan->entries[state.entry].entry;
    if (chunk.offset != state.received || chunk.data.size() > entry.byte_count - state.received) {
        fail(fetch, Error::IntegrityFailed);
        return;
    }
    state.received += chunk.data.size();
    if (entry.kind == EntryKind::Link) {
        state.link.append(chunk.data.begin(), chunk.data.end());
        return;
    }
    fetch.unwritten += chunk.data.size();
    executor_.run_file_work(
        [this, id = fetch.id, disk = fetch.disk, index = state.entry, data = chunk.data] {
            const Status<Error> status = write_entry(*disk, index, data);
            executor_.run_on_mesh([this, id, bytes = data.size(), status](TimePoint now) {
                written(id, bytes, status, now);
            });
        });
    if (!fetch.paused && fetch.unwritten >= tunables_.write_high_water) {
        fetch.paused = true;
        streams_.pause(key_of(fetch), true);
    }
}

void Receiver::received_end(Fetch& fetch, const protocol::ContentEnd& end) {
    const auto pending = fetch.pending.find(end.request_id);
    if (fetch.stage != Stage::Fetching || pending == fetch.pending.end()) {
        fail(fetch, Error::UnexpectedMessage);
        return;
    }
    const Pending state = std::move(pending->second);
    fetch.pending.erase(pending);
    const PlannedEntry& planned = fetch.plan->entries[state.entry];
    if (state.received != planned.entry.byte_count || end.byte_count != state.received) {
        fail(fetch, Error::IntegrityFailed);
        return;
    }
    if (planned.entry.kind == EntryKind::Link) {
        crypto::ContentHasher hasher;
        hasher.update(protocol::bytes_of(state.link));
        if (hasher.finish() != end.digest) {
            fail(fetch, Error::IntegrityFailed);
            return;
        }
        fetch.links.emplace_back(state.entry, state.link);
    } else {
        ++fetch.finishing;
        executor_.run_file_work([this, id = fetch.id, disk = fetch.disk, index = state.entry,
                                 entry = planned.entry, digest = end.digest,
                                 is_top_level = top_level(planned)] {
            const Status<Error> status = finish_entry(*disk, index, entry, digest, is_top_level);
            executor_.run_on_mesh([this, id, status](TimePoint now) { finished(id, status, now); });
        });
    }
    request_more(fetch);
}

void Receiver::written(TransferId id, std::uint64_t bytes, Status<Error> status, TimePoint now) {
    const auto it = fetches_.find(id);
    if (it == fetches_.end() || it->second.stage == Stage::Failing) {
        return;
    }
    Fetch& fetch = it->second;
    if (!status) {
        fail(fetch, status.error());
        return;
    }
    fetch.unwritten -= bytes;
    fetch.bytes_done += bytes;
    if (fetch.paused && fetch.unwritten < tunables_.write_low_water) {
        fetch.paused = false;
        streams_.pause(key_of(fetch), false);
    }
    report_progress(fetch, now, false);
}

void Receiver::finished(TransferId id, Status<Error> status, TimePoint now) {
    const auto it = fetches_.find(id);
    if (it == fetches_.end() || it->second.stage == Stage::Failing) {
        return;
    }
    Fetch& fetch = it->second;
    --fetch.finishing;
    if (!status) {
        fail(fetch, status.error());
        return;
    }
    ++fetch.files_done;
    report_progress(fetch, now, false);
    maybe_finalize(fetch);
}

void Receiver::maybe_finalize(Fetch& fetch) {
    if (fetch.stage != Stage::Fetching || fetch.next_entry < fetch.plan->entries.size() ||
        !fetch.pending.empty() || fetch.finishing > 0) {
        return;
    }
    fetch.stage = Stage::Finishing;
    executor_.run_file_work(
        [this, id = fetch.id, disk = fetch.disk, plan = fetch.plan, links = fetch.links] {
            const std::size_t skipped = finalize_disk(*disk, *plan, links);
            executor_.run_on_mesh([this, id, skipped, items = disk->items](TimePoint now) {
                finalized(id, skipped, items, now);
            });
        });
}

void Receiver::finalized(TransferId id, std::size_t skipped_links,
                         std::vector<std::filesystem::path> items, TimePoint now) {
    const auto it = fetches_.find(id);
    if (it == fetches_.end() || it->second.stage != Stage::Finishing) {
        return;
    }
    Fetch& fetch = it->second;
    report_progress(fetch, now, true);
    finish_stream(fetch);
    const mesh::DeviceId device = fetch.device;
    fetches_.erase(it);
    observer_.on_transfer_finished(TransferOutcome{.id = id,
                                                   .error = std::nullopt,
                                                   .items = std::move(items),
                                                   .skipped_links = skipped_links});
    start_waiting(device);
}

void Receiver::fail(Fetch& fetch, Error error) {
    if (fetch.stage == Stage::Failing) {
        return;
    }
    fetch.stage = Stage::Failing;
    fetch.error = error;
    finish_stream(fetch);
    executor_.run_file_work([this, id = fetch.id, disk = fetch.disk] {
        if (disk) {
            rollback(*disk);
        }
        executor_.run_on_mesh([this, id](TimePoint /*now*/) { rolled_back(id); });
    });
}

void Receiver::rolled_back(TransferId id) {
    const auto it = fetches_.find(id);
    if (it == fetches_.end()) {
        return;
    }
    const mesh::DeviceId device = it->second.device;
    const bool content = it->second.content;
    const std::optional<Error> error = it->second.error;
    fetches_.erase(it);
    if (content) {
        observer_.on_content_received(ContentOutcome{.id = id, .error = error, .content = {}});
    } else {
        observer_.on_transfer_finished(
            TransferOutcome{.id = id, .error = error, .items = {}, .skipped_links = 0});
    }
    start_waiting(device);
}

void Receiver::report_progress(Fetch& fetch, TimePoint now, bool force) {
    if (!force && fetch.last_progress && now - *fetch.last_progress < tunables_.progress_interval) {
        return;
    }
    fetch.last_progress = now;
    observer_.on_transfer_progress(TransferProgress{.id = fetch.id,
                                                    .bytes_done = fetch.bytes_done,
                                                    .bytes_total = fetch.plan->total_bytes,
                                                    .files_done = fetch.files_done,
                                                    .files_total = fetch.plan->file_count});
}

} // namespace osdrop::transfer
