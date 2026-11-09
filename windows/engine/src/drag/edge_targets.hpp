#pragma once

// Drop targets along the PC's edges (DEVPLAN Phase 4): thin, nearly invisible, topmost windows
// that never take the focus, shown only while the left button is down, so a drag carried to an
// edge lands on one. A drop is taken as a copy, so the source keeps its files: dragged files are
// read from CF_HDROP, virtual files (Outlook attachments, zip folders) are saved into a staging
// folder first, in the background when the source allows it. A source that runs elevated cannot
// reach them (UIPI; docs/TROUBLESHOOTING.md).

#include <osdrop/win/screens.hpp>

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace osdrop::engine::drag {

class EdgeTargets {
public:
    /// The dropped files and folders, on the targets' thread; `at` is where the drop happened, in
    /// physical pixels. Empty when the drop carried nothing that could be read.
    using Dropped = std::function<void(std::vector<std::filesystem::path> items, POINT at)>;

    explicit EdgeTargets(Dropped dropped);
    ~EdgeTargets();
    EdgeTargets(const EdgeTargets&) = delete;
    EdgeTargets& operator=(const EdgeTargets&) = delete;
    EdgeTargets(EdgeTargets&&) = delete;
    EdgeTargets& operator=(EdgeTargets&&) = delete;

    /// Where the targets are, in physical pixels; empty removes them. Any thread.
    void set_strips(std::vector<win::PixelRect> strips);

private:
    void run();
    void place();
    void poll();

    Dropped dropped_;
    std::mutex mutex_;
    std::vector<win::PixelRect> wanted_; ///< under mutex_
    std::atomic<DWORD> thread_id_{0};
    std::atomic<int> over_{0}; ///< drags over one of the targets, which keep them shown
    // The targets' thread only:
    std::vector<HWND> windows_;
    bool shown_ = false;
    std::thread thread_; // declared last: starts once everything above exists
};

} // namespace osdrop::engine::drag
