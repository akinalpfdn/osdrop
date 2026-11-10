#include "drag/loopback.hpp"

#include "drag/drag_starter.hpp"

#include <osdrop/win/drag_files.hpp>

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace osdrop::engine::drag {
namespace {

namespace shared = win::drag;

/// How thick the drop targets are, in physical pixels.
constexpr std::int32_t kStripThickness = 6;
/// The drag comes back this far inside the left edge, off the strip there.
constexpr LONG kStartInset = 48;
/// The starter window needs a moment to show before the press that starts the drag.
constexpr auto kPressDelay = std::chrono::milliseconds(150);

/// Local files read front to back; apps that take only paths get the files themselves.
class LocalContent final : public DragContent {
public:
    LocalContent(std::vector<shared::TreeEntry> entries, std::vector<std::filesystem::path> paths,
                 std::vector<std::filesystem::path> items)
        : entries_(std::move(entries)), paths_(std::move(paths)), items_(std::move(items)) {}

    std::optional<std::vector<shared::TreeEntry>> entries() override { return entries_; }
    std::optional<std::vector<std::filesystem::path>> local_items() override { return items_; }

    std::unique_ptr<shared::ContentReader> open(std::size_t entry) override {
        if (entry >= paths_.size()) {
            return nullptr;
        }
        auto reader = std::make_unique<Reader>(paths_[entry]);
        return reader->ok() ? std::move(reader) : nullptr;
    }

private:
    class Reader final : public shared::ContentReader {
    public:
        explicit Reader(const std::filesystem::path& path) : file_(path, std::ios::binary) {}
        [[nodiscard]] bool ok() const { return static_cast<bool>(file_); }
        std::size_t read(std::span<std::uint8_t> buffer) override {
            file_.read(reinterpret_cast<char*>(buffer.data()),
                       static_cast<std::streamsize>(buffer.size()));
            return static_cast<std::size_t>(file_.gcount());
        }

    private:
        std::ifstream file_;
    };

    std::vector<shared::TreeEntry> entries_;
    std::vector<std::filesystem::path> paths_;
    std::vector<std::filesystem::path> items_;
};

std::uint64_t write_time(const std::filesystem::path& path) {
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    // MSVC's file clock counts 100 ns since 1601, as FILETIME does.
    return error ? 0 : static_cast<std::uint64_t>(time.time_since_epoch().count());
}

std::string relative_name(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

/// Every file and folder of the dropped items, parents first, with where each one is.
std::pair<std::vector<shared::TreeEntry>, std::vector<std::filesystem::path>>
local_tree(const std::vector<std::filesystem::path>& items) {
    std::vector<shared::TreeEntry> entries;
    std::vector<std::filesystem::path> paths;
    std::error_code error;
    for (const std::filesystem::path& item : items) {
        const bool folder = std::filesystem::is_directory(item, error);
        entries.push_back({.path = relative_name(item.filename()),
                           .folder = folder,
                           .size = folder ? 0 : std::filesystem::file_size(item, error),
                           .write_time = write_time(item)});
        paths.push_back(item);
        if (!folder) {
            continue;
        }
        for (std::filesystem::recursive_directory_iterator it(item, error), end;
             !error && it != end; it.increment(error)) {
            const bool is_folder = it->is_directory(error);
            entries.push_back(
                {.path = relative_name(item.filename() /
                                       std::filesystem::relative(it->path(), item, error)),
                 .folder = is_folder,
                 .size = is_folder ? 0 : it->file_size(error),
                 .write_time = write_time(it->path())});
            paths.push_back(it->path());
        }
    }
    return {std::move(entries), std::move(paths)};
}

void press_left_button() {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; // NOLINT(cppcoreguidelines-pro-type-union-access)
    SendInput(1, &input, sizeof(INPUT));
}

const char* describe(DragOutcome outcome) {
    switch (outcome) {
    case DragOutcome::Dropped:
        return "Drag loopback: dropped";
    case DragOutcome::Cancelled:
        return "Drag loopback: cancelled";
    case DragOutcome::Failed:
        break;
    }
    return "Drag loopback: the drag did not start";
}

} // namespace

Loopback::Loopback(Log log) : log_(std::move(log)) {}

void Loopback::set_enabled(bool enabled) {
    const std::scoped_lock lock(mutex_);
    enabled_ = enabled;
    update();
}

void Loopback::set_monitors(std::vector<win::PhysicalMonitor> monitors) {
    {
        const std::scoped_lock lock(monitors_mutex_);
        monitors_ = std::move(monitors);
    }
    const std::scoped_lock lock(mutex_);
    update();
}

std::vector<win::PhysicalMonitor> Loopback::monitors() {
    const std::scoped_lock lock(monitors_mutex_);
    return monitors_;
}

void Loopback::update() {
    if (!enabled_) {
        targets_.reset();
        return;
    }
    if (!targets_) {
        targets_ = std::make_unique<EdgeTargets>(
            EdgeTargets::Handler{.entered = {},
                                 .taking = {}, // every drop is taken
                                 .taken = [this](const std::vector<std::filesystem::path>& items,
                                                 POINT at) { dropped(items, at); },
                                 .released = {}});
    }
    targets_->set_strips(shared::edge_strips(monitors(), kStripThickness));
}

void Loopback::dropped(const std::vector<std::filesystem::path>& items, POINT at) {
    if (items.empty()) {
        log_("Drag loopback: the drop carried nothing that could be read");
        return;
    }
    auto [entries, paths] = local_tree(items);
    POINT start{.x = at.x, .y = at.y};
    for (const win::PhysicalMonitor& monitor : monitors()) {
        if (at.x >= monitor.left && at.x < monitor.right && at.y >= monitor.top &&
            at.y < monitor.bottom) {
            start.x = monitor.left + kStartInset;
        }
    }
    const std::string count = std::to_string(entries.size());
    log_(("Drag loopback: captured " + count + " files and folders").c_str());
    SetCursorPos(start.x, start.y);
    std::vector<shared::ArrivingItem> top;
    std::error_code error;
    for (const std::filesystem::path& item : items) {
        const bool folder = std::filesystem::is_directory(item, error);
        std::uint64_t size = 0;
        for (const shared::TreeEntry& entry : entries) {
            const bool inside = entry.path == relative_name(item.filename()) ||
                                entry.path.starts_with(relative_name(item.filename()) + "/");
            size += inside ? entry.size : 0;
        }
        top.push_back({.name = relative_name(item.filename()), .folder = folder, .size = size});
    }
    const Log log = log_;
    const auto handle = start_drag(DragRequest{.at = start,
                                               .items = std::move(top),
                                               .content = std::make_shared<LocalContent>(
                                                   std::move(entries), std::move(paths), items),
                                               .staged_limit = DragRequest{}.staged_limit},
                                   [log](DragOutcome outcome) { log(describe(outcome)); });
    if (!handle) {
        log_("Drag loopback: the drag thread did not start");
        return;
    }
    std::thread([] {
        std::this_thread::sleep_for(kPressDelay);
        press_left_button(); // the drag starts; the user's click drops it
    }).detach();
}

} // namespace osdrop::engine::drag
