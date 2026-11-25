#include <osdrop/win/sent_files.hpp>

#include <utility>

namespace osdrop::win {
namespace {

/// Transfers whose end is remembered while their start is not known yet.
constexpr std::size_t kEarlyEnds = 64;

} // namespace

SentFiles::SentFiles(SentMesh& mesh, SentReceiving receiving, std::filesystem::path folder)
    : mesh_(mesh), receiving_(std::move(receiving)), folder_(std::move(folder)),
      worker_([this] { run(); }) {}

SentFiles::~SentFiles() {
    stop();
}

void SentFiles::stop() {
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void SentFiles::post(std::function<void()> work) {
    {
        const std::scoped_lock lock(mutex_);
        if (stopping_) {
            return;
        }
        work_.push_back(std::move(work));
    }
    wake_.notify_one();
}

void SentFiles::run() {
    for (;;) {
        std::function<void()> work;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !work_.empty(); });
            if (work_.empty()) {
                return; // stopping, and everything queued ran
            }
            work = std::move(work_.front());
            work_.pop_front();
        }
        work();
    }
}

void SentFiles::arrived(const SenderId& device, std::uint64_t send_id,
                        std::vector<SentItem> items) {
    if (folder_.empty() || items.empty()) {
        post([this, device, send_id] { mesh_.answer(device, send_id, OSDROP_SEND_DECLINED); });
        return;
    }
    {
        const std::scoped_lock lock(mutex_);
        if (arrivals_.contains(send_id)) {
            return;
        }
        arrivals_[send_id] = Arrival{.device = device, .pending = items.size(), .failed = false};
    }
    post([this, device, send_id, items = std::move(items)] { fetch(device, send_id, items); });
}

void SentFiles::fetch(const SenderId& device, std::uint64_t send_id,
                      const std::vector<SentItem>& items) {
    for (std::uint32_t index = 0; index < items.size(); ++index) {
        const std::optional<std::uint64_t> transfer =
            mesh_.receive(device, send_id, index, folder_);
        if (!transfer) {
            item_ended(send_id, false);
            continue;
        }
        receiving_(*transfer, device, items[index].name, items[index].size);
        std::optional<bool> ended;
        {
            const std::scoped_lock lock(mutex_);
            if (const auto early = early_.find(*transfer); early != early_.end()) {
                ended = early->second;
                early_.erase(early);
            } else {
                sends_[*transfer] = send_id;
            }
        }
        if (ended) {
            item_ended(send_id, *ended);
        }
    }
}

void SentFiles::finished(std::uint64_t transfer_id, osdrop_result result) {
    std::optional<std::uint64_t> send_id;
    {
        const std::scoped_lock lock(mutex_);
        if (const auto found = sends_.find(transfer_id); found != sends_.end()) {
            send_id = found->second;
            sends_.erase(found);
        } else {
            early_[transfer_id] = result == OSDROP_OK;
            if (early_.size() > kEarlyEnds) {
                early_.erase(early_.begin());
            }
        }
    }
    if (send_id) {
        item_ended(*send_id, result == OSDROP_OK);
    }
}

void SentFiles::item_ended(std::uint64_t send_id, bool succeeded) {
    Arrival done;
    {
        const std::scoped_lock lock(mutex_);
        const auto arrival = arrivals_.find(send_id);
        if (arrival == arrivals_.end()) {
            return;
        }
        arrival->second.failed = arrival->second.failed || !succeeded;
        if (--arrival->second.pending > 0) {
            return;
        }
        done = arrival->second;
        arrivals_.erase(arrival);
    }
    const osdrop_send_outcome outcome = done.failed ? OSDROP_SEND_FAILED : OSDROP_SEND_RECEIVED;
    post(
        [this, device = done.device, send_id, outcome] { mesh_.answer(device, send_id, outcome); });
}

} // namespace osdrop::win
