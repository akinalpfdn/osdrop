#include <osdrop/win/network_drag.hpp>

#include <algorithm>
#include <string>
#include <system_error>
#include <utility>

namespace osdrop::win::drag {
namespace {

/// Events kept while a receive() runs; a bound, should one never return.
constexpr std::size_t kMaxKeptEvents = 4096;
/// A file that cannot be opened or read yet (moving to its place) is tried again after this.
constexpr std::chrono::milliseconds kRetryInterval{20};
/// 100 ns intervals from 1601 to 1970, as FILETIME counts them.
constexpr std::int64_t kFileTimeEpoch = 116'444'736'000'000'000;
constexpr std::int64_t kFileTimePerMicrosecond = 10;
constexpr std::uint8_t kFolder = 2;
constexpr std::uint8_t kLink = 3;

std::uint64_t file_time(std::int64_t modified_us) {
    const std::int64_t ticks = (modified_us * kFileTimePerMicrosecond) + kFileTimeEpoch;
    return ticks > 0 ? static_cast<std::uint64_t>(ticks) : 0;
}

} // namespace

// --- TransferRouter ------------------------------------------------------------------------------

void TransferRouter::starting() {
    const std::scoped_lock lock(mutex_);
    ++starting_;
}

void TransferRouter::started(std::optional<std::uint64_t> transfer_id,
                             const std::weak_ptr<TransferSink>& sink) {
    // Delivered under the lock, so the kept events reach the sink before newer ones.
    const std::scoped_lock lock(mutex_);
    if (transfer_id) {
        sinks_[*transfer_id] = sink;
        const std::shared_ptr<TransferSink> target = sink.lock();
        for (auto& [id, event] : kept_) {
            if (id == *transfer_id && target) {
                event(*target);
            }
        }
        std::erase_if(kept_, [&](const auto& kept) { return kept.first == *transfer_id; });
    }
    if (--starting_ == 0) {
        kept_.clear(); // other transfers' events: not a drag's
    }
}

std::shared_ptr<TransferSink> TransferRouter::sink_or_keep(std::uint64_t transfer_id, Event event) {
    const auto it = sinks_.find(transfer_id);
    if (it == sinks_.end()) {
        if (starting_ > 0 && kept_.size() < kMaxKeptEvents) {
            kept_.emplace_back(transfer_id, std::move(event));
        }
        return nullptr;
    }
    return it->second.lock();
}

void TransferRouter::planned(std::uint64_t transfer_id, const std::vector<PlannedEntry>& entries) {
    const std::scoped_lock lock(mutex_);
    const Event event = [transfer_id, copy = entries](TransferSink& sink) {
        sink.planned(transfer_id, copy);
    };
    if (const auto sink = sink_or_keep(transfer_id, event)) {
        event(*sink);
    }
}

void TransferRouter::written(std::uint64_t transfer_id, std::uint32_t entry,
                             const std::filesystem::path& path, std::uint64_t bytes,
                             bool complete) {
    const std::scoped_lock lock(mutex_);
    const Event event = [transfer_id, entry, copy = path, bytes, complete](TransferSink& sink) {
        sink.written(transfer_id, entry, copy, bytes, complete);
    };
    if (const auto sink = sink_or_keep(transfer_id, event)) {
        event(*sink);
    }
}

void TransferRouter::finished(std::uint64_t transfer_id, osdrop_result result,
                              const std::vector<std::filesystem::path>& items) {
    const std::scoped_lock lock(mutex_);
    const Event event = [transfer_id, result, copy = items](TransferSink& sink) {
        sink.finished(transfer_id, result, copy);
    };
    if (const auto sink = sink_or_keep(transfer_id, event)) {
        event(*sink);
    }
    sinks_.erase(transfer_id);
}

// --- NetworkDrag ---------------------------------------------------------------------------------

/// One file read front to back as core writes it.
class NetworkDrag::Reader final : public ContentReader {
public:
    Reader(std::shared_ptr<NetworkDrag> drag, Place place)
        : drag_(std::move(drag)), place_(place) {}

    std::size_t read(std::span<std::uint8_t> buffer) override {
        while (!buffer.empty()) {
            std::filesystem::path path;
            std::uint64_t readable = 0;
            bool complete = false;
            {
                std::unique_lock lock(drag_->mutex_);
                drag_->changed_.wait(lock, [&] { return drag_->broken() || ready(); });
                const File* file = this->file();
                if (drag_->broken() || file == nullptr) {
                    return 0; // the transfer failed or the drag was cancelled
                }
                if (file->complete && position_ >= file->bytes) {
                    return 0; // the end
                }
                path = file->path;
                complete = file->complete;
                readable = available(*file) - position_;
            }
            const auto wanted =
                static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), readable));
            if (const std::size_t got = read_at(path, buffer.first(wanted)).value_or(0); got > 0) {
                position_ += got;
                return got;
            }
            if (complete) {
                return 0; // a finished file that cannot be read as core said: broken
            }
            file_.reset(); // moving to its place: open it again there
            std::unique_lock lock(drag_->mutex_);
            drag_->changed_.wait_for(lock, kRetryInterval);
        }
        return 0;
    }

private:
    /// Under the drag's mutex.
    [[nodiscard]] const File* file() const {
        const Item& item = drag_->state_[place_.item];
        const auto it = item.files.find(place_.entry);
        return it == item.files.end() ? nullptr : &it->second;
    }
    [[nodiscard]] static std::uint64_t available(const File& file) {
        // The last byte waits for the check: a drop never completes a file that failed it.
        if (file.complete) {
            return file.bytes;
        }
        return file.bytes > 0 ? file.bytes - 1 : 0;
    }
    [[nodiscard]] bool ready() const {
        const File* file = this->file();
        return file != nullptr && !file->path.empty() &&
               (available(*file) > position_ || (file->complete && position_ >= file->bytes));
    }

    std::optional<std::size_t> read_at(const std::filesystem::path& path,
                                       std::span<std::uint8_t> buffer) {
        if (!file_) {
            file_ = drag_->opener_(path);
            if (!file_) {
                return std::nullopt;
            }
        }
        return file_->read(position_, buffer);
    }

    std::shared_ptr<NetworkDrag> drag_;
    Place place_;
    std::uint64_t position_ = 0;
    std::unique_ptr<GrowingFile> file_; // kept open: it stays readable when core moves it
};

NetworkDrag::NetworkDrag(DragTransfers& transfers, const SourceId& source, std::uint32_t drag_id,
                         std::vector<ArrivingItem> items, std::filesystem::path staging,
                         FileOpener opener)
    : transfers_(transfers), source_(source), drag_id_(drag_id), items_(std::move(items)),
      staging_(std::move(staging)), opener_(std::move(opener)), state_(items_.size()) {}

bool NetworkDrag::start() {
    for (std::size_t index = 0; index < items_.size(); ++index) {
        const std::filesystem::path folder = staging_ / std::to_string(index);
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        {
            const std::scoped_lock lock(mutex_);
            starting_ = index;
        }
        const std::optional<std::uint64_t> id =
            error ? std::nullopt
                  : transfers_.receive(source_, drag_id_, static_cast<std::uint32_t>(index), folder,
                                       weak_from_this());
        {
            const std::scoped_lock lock(mutex_);
            starting_.reset();
            if (id && !state_[index].transfer) {
                state_[index].transfer = id;
            }
            failed_ = failed_ || !id;
            changed_.notify_all();
        }
        if (!id) {
            cancel();
            return false;
        }
    }
    return true;
}

std::optional<std::vector<TreeEntry>> NetworkDrag::entries(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    const auto planned_all = [&] {
        return std::ranges::all_of(state_, [](const Item& item) { return item.plan.has_value(); });
    };
    if (!changed_.wait_for(lock, timeout, [&] { return broken() || planned_all(); }) || broken()) {
        return std::nullopt;
    }
    std::vector<TreeEntry> tree;
    std::vector<Place> places;
    for (std::size_t index = 0; index < state_.size(); ++index) {
        const std::optional<std::vector<PlannedEntry>>& item_plan = state_[index].plan;
        if (!item_plan) {
            return std::nullopt; // cannot happen: every item was planned above
        }
        const std::vector<PlannedEntry>& plan = *item_plan;
        for (std::size_t entry = 0; entry < plan.size(); ++entry) {
            const PlannedEntry& planned = plan[entry];
            if (planned.kind == kLink) {
                continue; // a virtual file cannot be a link
            }
            tree.push_back({.path = planned.path,
                            .folder = planned.kind == kFolder,
                            .size = planned.kind == kFolder ? 0 : planned.size,
                            .write_time = file_time(planned.modified_us)});
            places.push_back({.item = index, .entry = static_cast<std::uint32_t>(entry)});
        }
    }
    places_ = std::move(places);
    return tree;
}

std::unique_ptr<ContentReader> NetworkDrag::open(std::size_t entry) {
    Place place;
    {
        const std::scoped_lock lock(mutex_);
        if (!places_ || entry >= places_->size()) {
            return nullptr;
        }
        place = (*places_)[entry];
    }
    return std::make_unique<Reader>(shared_from_this(), place);
}

std::optional<std::vector<std::filesystem::path>>
NetworkDrag::received(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    const auto all_done = [&] {
        return std::ranges::all_of(state_, [](const Item& item) { return item.done; });
    };
    if (!changed_.wait_for(lock, timeout, [&] { return broken() || all_done(); }) || broken()) {
        return std::nullopt;
    }
    std::vector<std::filesystem::path> paths;
    for (const Item& item : state_) {
        paths.insert(paths.end(), item.received.begin(), item.received.end());
    }
    return paths;
}

void NetworkDrag::cancel() {
    std::vector<std::uint64_t> running;
    {
        const std::scoped_lock lock(mutex_);
        if (cancelled_) {
            return;
        }
        cancelled_ = true;
        for (const Item& item : state_) {
            if (item.transfer && !item.done) {
                running.push_back(*item.transfer);
            }
        }
        changed_.notify_all();
    }
    for (const std::uint64_t id : running) {
        transfers_.cancel(id);
    }
}

std::vector<std::optional<std::uint64_t>> NetworkDrag::transfers() {
    const std::scoped_lock lock(mutex_);
    std::vector<std::optional<std::uint64_t>> ids;
    ids.reserve(state_.size());
    for (const Item& item : state_) {
        ids.push_back(item.transfer);
    }
    return ids;
}

std::uint64_t NetworkDrag::total_bytes() const {
    std::uint64_t total = 0;
    for (const ArrivingItem& item : items_) {
        total += item.size;
    }
    return total;
}

NetworkDrag::Item* NetworkDrag::item_of(std::uint64_t transfer_id) {
    for (Item& item : state_) {
        if (item.transfer == transfer_id) {
            return &item;
        }
    }
    if (starting_ && !state_[*starting_].transfer) {
        state_[*starting_].transfer = transfer_id; // its events came before its ID
        return &state_[*starting_];
    }
    return nullptr;
}

void NetworkDrag::planned(std::uint64_t transfer_id, const std::vector<PlannedEntry>& entries) {
    const std::scoped_lock lock(mutex_);
    if (Item* item = item_of(transfer_id)) {
        item->plan = entries;
        changed_.notify_all();
    }
}

void NetworkDrag::written(std::uint64_t transfer_id, std::uint32_t entry,
                          const std::filesystem::path& path, std::uint64_t bytes, bool complete) {
    const std::scoped_lock lock(mutex_);
    if (Item* item = item_of(transfer_id)) {
        item->files[entry] = File{.path = path, .bytes = bytes, .complete = complete};
        changed_.notify_all();
    }
}

void NetworkDrag::finished(std::uint64_t transfer_id, osdrop_result result,
                           const std::vector<std::filesystem::path>& items) {
    const std::scoped_lock lock(mutex_);
    if (Item* item = item_of(transfer_id)) {
        item->done = true;
        if (result == OSDROP_OK) {
            item->received = items;
        } else {
            failed_ = true;
        }
        changed_.notify_all();
    }
}

} // namespace osdrop::win::drag
