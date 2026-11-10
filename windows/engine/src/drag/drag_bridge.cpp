#include "drag/drag_bridge.hpp"

#include "input/input_bridge.hpp"

#include <osdrop/win/drag_files.hpp>
#include <osdrop/win/injection.hpp>

#include <windows.h>

#include <algorithm>
#include <exception>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace osdrop::engine::drag {
namespace {

namespace fs = std::filesystem;
namespace shared = win::drag;

/// How thick the drop targets along the edges are, in physical pixels.
constexpr std::int32_t kStripThickness = 6;
/// The window an arriving drag starts from shows this long before the press reaches it.
constexpr auto kPressDelay = std::chrono::milliseconds(150);
/// A wanted drag's drop must reach the targets this soon after its button is released.
constexpr auto kTakeTimeout = std::chrono::seconds(3);
/// Arriving drags up to this size are also offered as paths, received in full before an app that
/// asks for paths gets them; it waits on that.
constexpr std::uint64_t kStagedLimit = std::uint64_t{16} * 1024 * 1024;
/// How long a target asking for the files' list or paths waits for them.
constexpr auto kListWait = std::chrono::seconds(60);
constexpr auto kPathsWait = std::chrono::seconds(60);
/// Staging folders whose paths an app was given, kept for uploads that read them later.
constexpr std::size_t kKeptGiven = 4;
constexpr DWORD kMaxRead = DWORD{1} << 30U;
constexpr std::uint8_t kButtonDown = 1;

/// A file core is writing, read through its own handle; sharing lets core keep writing it, move
/// it to its place and delete it, and the handle reads it wherever it went.
class SharedFile final : public shared::GrowingFile {
public:
    explicit SharedFile(HANDLE handle) : handle_(handle) {}
    ~SharedFile() override { CloseHandle(handle_); }
    SharedFile(const SharedFile&) = delete;
    SharedFile& operator=(const SharedFile&) = delete;
    SharedFile(SharedFile&&) = delete;
    SharedFile& operator=(SharedFile&&) = delete;

    std::optional<std::size_t> read(std::uint64_t offset, std::span<std::uint8_t> buffer) override {
        OVERLAPPED at{};
        // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access): OVERLAPPED's offset members
        at.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFU);
        at.OffsetHigh = static_cast<DWORD>(offset >> 32U);
        // NOLINTEND(cppcoreguidelines-pro-type-union-access)
        const auto wanted = static_cast<DWORD>(std::min<std::size_t>(buffer.size(), kMaxRead));
        DWORD got = 0;
        if (ReadFile(handle_, buffer.data(), wanted, &got, &at) == FALSE) {
            return GetLastError() == ERROR_HANDLE_EOF ? std::optional<std::size_t>(0)
                                                      : std::nullopt;
        }
        return got;
    }

private:
    HANDLE handle_;
};

std::unique_ptr<shared::GrowingFile> open_shared(const fs::path& path) {
    HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    return std::make_unique<SharedFile>(handle);
}

/// An arriving drag's files as the drag starter reads them.
class NetworkContent final : public DragContent {
public:
    NetworkContent(std::shared_ptr<shared::NetworkDrag> drag,
                   std::shared_ptr<std::atomic<bool>> paths_given)
        : drag_(std::move(drag)), paths_given_(std::move(paths_given)) {}

    std::optional<std::vector<shared::TreeEntry>> entries() override {
        return drag_->entries(kListWait);
    }
    std::optional<std::vector<fs::path>> local_items() override {
        auto items = drag_->received(kPathsWait);
        if (items) {
            *paths_given_ = true; // the app may read them after the drag
        }
        return items;
    }
    std::unique_ptr<shared::ContentReader> open(std::size_t entry) override {
        return drag_->open(entry);
    }

private:
    std::shared_ptr<shared::NetworkDrag> drag_;
    std::shared_ptr<std::atomic<bool>> paths_given_;
};

osdrop_drag_outcome outcome_of(DragOutcome outcome) {
    switch (outcome) {
    case DragOutcome::Dropped:
        return OSDROP_DRAG_DROPPED;
    case DragOutcome::Cancelled:
        return OSDROP_DRAG_CANCELLED;
    case DragOutcome::Failed:
        break;
    }
    return OSDROP_DRAG_FAILED;
}

const char* describe(DragOutcome outcome) {
    switch (outcome) {
    case DragOutcome::Dropped:
        return "A drag from another computer was dropped here";
    case DragOutcome::Cancelled:
        return "A drag from another computer was cancelled";
    case DragOutcome::Failed:
        break;
    }
    return "A drag from another computer did not start";
}

osdrop_input left_press(std::int32_t x, std::int32_t y) {
    return osdrop_input{.kind = OSDROP_INPUT_BUTTON,
                        .x = x,
                        .y = y,
                        .dx = 0,
                        .dy = 0,
                        .page = 0,
                        .usage = 0,
                        .action = kButtonDown,
                        .button = 0,
                        .click_count = 1,
                        .scroll_unit = 0,
                        .scroll_phase = 0,
                        .momentum = 0};
}

void remove_folder(const fs::path& folder) {
    std::error_code ignored;
    fs::remove_all(folder, ignored);
}

} // namespace

DragBridge::DragBridge(win::GroupEngine& group, InputBridge& input, fs::path staging,
                       Listener listener)
    : group_(group), input_(input), staging_(std::move(staging)), listener_(std::move(listener)),
      targets_(std::make_unique<EdgeTargets>(EdgeTargets::Handler{
          .entered =
              [this](std::vector<fs::path> files, bool virtual_files) {
                  post([this, files = std::move(files), virtual_files]() mutable {
                      report(std::move(files), virtual_files);
                  });
              },
          .taking = [this] { return taking_.load(); },
          .taken =
              [this](std::vector<fs::path> items, POINT /*at*/) {
                  post([this, items = std::move(items)] { take_ended(items); });
              },
          .released = [this] { post([this] { release(); }); }})),
      thread_([this] { run(); }) {
    remove_folder(staging_); // what the last run left
}

DragBridge::~DragBridge() {
    try {
        stop();
    } catch (...) {
        std::terminate(); // only joining can throw: unrecoverable
    }
}

void DragBridge::stop() {
    {
        const std::scoped_lock lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
        tasks_.clear();
        timers_.clear();
        wake_.notify_one();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (arrival_) {
        arrival_->handle->cancel();
        arrival_->network->cancel(); // before the group engine goes
    }
    targets_.reset();
}

void DragBridge::set_active(bool active) {
    {
        const std::scoped_lock lock(mutex_);
        active_ = active;
    }
    update_strips();
}

void DragBridge::set_paused(bool paused) {
    {
        const std::scoped_lock lock(mutex_);
        paused_ = paused;
    }
    update_strips();
}

void DragBridge::set_monitors(std::vector<win::PhysicalMonitor> monitors) {
    {
        const std::scoped_lock lock(mutex_);
        monitors_ = std::move(monitors);
    }
    update_strips();
}

void DragBridge::update_strips() {
    std::vector<win::PixelRect> strips;
    {
        const std::scoped_lock lock(mutex_);
        if (active_ && !paused_ && !stopping_) {
            strips = shared::edge_strips(monitors_, kStripThickness);
        }
    }
    if (targets_) {
        targets_->set_strips(std::move(strips));
    }
}

void DragBridge::log(const char* message) const {
    if (listener_.log) {
        listener_.log(message);
    }
}

void DragBridge::post(std::function<void()> task) {
    const std::scoped_lock lock(mutex_);
    if (!stopping_) {
        tasks_.push_back(std::move(task));
        wake_.notify_one();
    }
}

void DragBridge::post_after(Clock::duration delay, std::function<void()> task) {
    const std::scoped_lock lock(mutex_);
    if (!stopping_) {
        timers_.emplace_back(Clock::now() + delay, std::move(task));
        wake_.notify_one();
    }
}

void DragBridge::run() {
    std::unique_lock lock(mutex_);
    while (!stopping_) {
        const auto next =
            std::ranges::min_element(timers_, {}, &decltype(timers_)::value_type::first);
        const bool timed = next != timers_.end();
        const auto ready = [this] { return stopping_ || !tasks_.empty(); };
        if (timed) {
            wake_.wait_until(lock, next->first, ready);
        } else {
            wake_.wait(lock, ready);
        }
        if (stopping_) {
            break;
        }
        std::vector<std::function<void()>> work(std::make_move_iterator(tasks_.begin()),
                                                std::make_move_iterator(tasks_.end()));
        tasks_.clear();
        const Clock::time_point now = Clock::now();
        for (auto it = timers_.begin(); it != timers_.end();) {
            if (it->first <= now) {
                work.push_back(std::move(it->second));
                it = timers_.erase(it);
            } else {
                ++it;
            }
        }
        lock.unlock();
        for (const auto& task : work) {
            task();
        }
        lock.lock();
    }
}

// --- This PC as the source -----------------------------------------------------------------------

void DragBridge::report(std::vector<fs::path> files, bool virtual_files) {
    if (reported_ || taking_files_) {
        return; // once per drag: it reaches the strips again and again
    }
    if (!files.empty()) {
        if (group_.drag_started(files)) {
            reported_ = true;
            log(("A drag of " + std::to_string(files.size()) + " items reached an edge").c_str());
        }
    } else if (virtual_files) {
        group_.drag_promised();
        reported_ = true;
        log("A drag of virtual files reached an edge");
    }
}

void DragBridge::release() {
    if (taking_files_) {
        return; // the files a wanted drag dropped are still being read
    }
    taking_ = DropTaking::Refuse;
    targets_->set_hold(std::nullopt);
    if (reported_.exchange(false)) {
        group_.drag_stopped();
    }
}

void DragBridge::ended(std::uint32_t /*drag_id*/, osdrop_drag_outcome outcome) {
    post([this, outcome] {
        taking_ = outcome == OSDROP_DRAG_DROPPED ? DropTaking::Accept : DropTaking::Refuse;
        log(outcome == OSDROP_DRAG_DROPPED ? "This PC's drag was dropped elsewhere: it ends here"
                                           : "This PC's drag was not dropped: it ends here");
        input_.release_drag(); // on the hold window or the strip it waits on
    });
}

void DragBridge::wanted() {
    post([this] {
        taking_ = DropTaking::Take;
        taking_files_ = true;
        const std::uint64_t attempt = ++take_attempt_;
        log("Taking the files of this PC's drag at the edge");
        input_.release_drag();
        post_after(kTakeTimeout, [this, attempt] {
            if (taking_files_ && take_attempt_ == attempt) {
                log("The drop of a wanted drag did not reach the edge");
                end_taking({});
            }
        });
    });
}

void DragBridge::take_ended(const std::vector<fs::path>& items) {
    if (taking_files_) {
        end_taking(items);
    }
}

void DragBridge::end_taking(const std::vector<fs::path>& items) {
    taking_files_ = false;
    taking_ = DropTaking::Refuse;
    reported_ = false; // the drag ended at the edge; its files travel as a new one
    targets_->set_hold(std::nullopt);
    static_cast<void>(group_.drag_started(items)); // none: they did not come
}

void DragBridge::capture_ended() {
    post([this] { targets_->set_hold(std::nullopt); });
}

void DragBridge::parking(POINT park) {
    if (reported_) {
        targets_->set_hold(park); // the cursor parks on it, and the drag with it
    }
}

// --- This PC as the target -----------------------------------------------------------------------

void DragBridge::arrived(const win::DragArrival& arrival) {
    post([this, copy = arrival] { arrive(copy); }); // a copy that can move: not const
}

void DragBridge::withdrawn(std::uint32_t drag_id) {
    post([this, drag_id] {
        if (arrival_ && arrival_->drag_id == drag_id) {
            arrival_->handle->cancel();
        }
    });
}

void DragBridge::arrive(const win::DragArrival& arrival) {
    if (arrival_) {
        arrival_->handle->cancel(); // a newer drag replaces it
    }
    const fs::path staging =
        staging_ / (std::to_string(arrival.drag_id) + "-" + std::to_string(GetTickCount64()));
    auto network = std::make_shared<shared::NetworkDrag>(group_, arrival.source, arrival.drag_id,
                                                         arrival.items, staging, open_shared);
    if (!network->start()) {
        log("The items of a drag that arrived could not be requested");
        group_.drop_finished(arrival.drag_id, OSDROP_DRAG_FAILED);
        remove_folder(staging);
        return;
    }
    auto entry =
        std::make_shared<Arrival>(Arrival{.drag_id = arrival.drag_id,
                                          .network = network,
                                          .paths_given = std::make_shared<std::atomic<bool>>(false),
                                          .handle = nullptr,
                                          .staging = staging});
    const std::uint32_t drag_id = arrival.drag_id;
    entry->handle = start_drag(
        DragRequest{.at = POINT{.x = win::Injection::pixel(arrival.x),
                                .y = win::Injection::pixel(arrival.y)},
                    .items = arrival.items,
                    .content = std::make_shared<NetworkContent>(network, entry->paths_given),
                    .staged_limit = kStagedLimit},
        [this, drag_id](DragOutcome outcome) {
            post([this, drag_id, outcome] { arrival_ended(drag_id, outcome); });
        },
        [this, entry] { post([this, entry] { arrival_done(entry); }); });
    if (!entry->handle) {
        network->cancel();
        group_.drop_finished(drag_id, OSDROP_DRAG_FAILED);
        remove_folder(staging);
        return;
    }
    arrival_ = entry;
    log(("A drag of " + std::to_string(arrival.items.size()) + " items arrived").c_str());
    const auto transfers = network->transfers();
    for (std::size_t index = 0; index < transfers.size() && listener_.receiving; ++index) {
        if (const std::optional<std::uint64_t>& transfer = transfers[index]; transfer.has_value()) {
            listener_.receiving(transfer.value(), arrival.source, arrival.items[index].name,
                                arrival.items[index].size);
        }
    }
    // The window the drag starts from shows before the press reaches it.
    post_after(kPressDelay, [this, entry, x = arrival.x, y = arrival.y] {
        if (arrival_ == entry && !entry->handle->cancelled()) {
            input_.inject(left_press(x, y));
        }
    });
}

void DragBridge::arrival_ended(std::uint32_t drag_id, DragOutcome outcome) {
    log(describe(outcome));
    group_.drop_finished(drag_id, outcome_of(outcome)); // ignored once withdrawn
}

void DragBridge::arrival_done(const std::shared_ptr<Arrival>& arrival) {
    arrival->network->cancel(); // what still runs: the drop never asked for it
    if (*arrival->paths_given) {
        given_.push_back(arrival->staging);
        while (given_.size() > kKeptGiven) {
            remove_folder(given_.front());
            given_.erase(given_.begin());
        }
    } else {
        remove_folder(arrival->staging);
    }
    if (arrival_ == arrival) {
        arrival_.reset();
    }
}

} // namespace osdrop::engine::drag
