#pragma once

// Drop targets along the PC's edges (DEVPLAN Phases 4 and 14): thin, nearly invisible, topmost
// windows that never take the focus, shown only while the left button is down, so a drag carried
// to an edge lands on one. A drag reaching a strip is reported with what it carries; how a drop is
// taken is asked at the drop: refused, accepted as a copy with nothing read (the files went to
// another computer), or taken, its files read and virtual files (Outlook attachments, zip folders)
// saved into a staging folder, in the background when the source allows it. While the cursor is
// on another computer the PC's input is captured and its cursor parked; a hold window at the park
// point keeps a drag of this PC there until its outcome. Osdrop's own drags are left alone. A
// source that runs elevated cannot reach the targets (UIPI; docs/TROUBLESHOOTING.md).

#include <osdrop/win/screens.hpp>

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace osdrop::engine::drag {

enum class DropTaking : std::uint8_t {
    Refuse, ///< the drag ends with nothing taken
    Accept, ///< a copy, with nothing read: its files went elsewhere
    Take,   ///< a copy, its files read (and virtual files saved) and handed on
};

class EdgeTargets {
public:
    /// Called on the targets' thread (`taken` also on a thread of its own); they must return
    /// quickly.
    struct Handler {
        /// A drag reached a strip: its files, or none and `virtual_files` when it carries only
        /// virtual files. Not for Osdrop's own drags.
        std::function<void(std::vector<std::filesystem::path> files, bool virtual_files)> entered;
        /// How a drop is taken now.
        std::function<DropTaking()> taking;
        /// What a Take drop carried: its files, or its virtual files saved into staging; empty
        /// when nothing could be read. `at` is where it was dropped, in physical pixels.
        std::function<void(std::vector<std::filesystem::path> items, POINT at)> taken;
        /// The left button went up with no drag over a target.
        std::function<void()> released;
    };

    explicit EdgeTargets(Handler handler);
    ~EdgeTargets();
    EdgeTargets(const EdgeTargets&) = delete;
    EdgeTargets& operator=(const EdgeTargets&) = delete;
    EdgeTargets(EdgeTargets&&) = delete;
    EdgeTargets& operator=(EdgeTargets&&) = delete;

    /// Where the strips are, in physical pixels; empty removes them. Any thread.
    void set_strips(std::vector<win::PixelRect> strips);
    /// The hold window around `at` (physical pixels), or none; returns once it is in place.
    /// Any thread but the targets'.
    void set_hold(std::optional<POINT> at);

private:
    void run();
    void place();
    void place_hold();
    void poll();

    Handler handler_;
    std::mutex mutex_;
    std::condition_variable hold_changed_;
    std::vector<win::PixelRect> wanted_; ///< under mutex_
    std::optional<POINT> hold_wanted_;   ///< under mutex_
    std::uint64_t hold_requests_ = 0;    ///< under mutex_
    std::uint64_t hold_placed_ = 0;      ///< under mutex_
    std::atomic<DWORD> thread_id_{0};
    std::atomic<int> over_{0}; ///< drags over one of the targets, which keep them shown
    // The targets' thread only:
    std::vector<HWND> windows_;
    HWND hold_ = nullptr;
    bool shown_ = false;
    bool held_down_ = false;
    std::thread thread_; // declared last: starts once everything above exists
};

} // namespace osdrop::engine::drag
