#pragma once

// Starting a drag on the PC from injected input (DEVPLAN Phase 4). A small, nearly invisible
// window waits under a point; the left button pressed there (by the user's hand or injected)
// starts DoDragDrop on the window's own STA thread, and the moves that follow carry the drag to
// the drop. The data object offers the files as virtual files: a FILEGROUPDESCRIPTORW for every
// file and folder, and a stream per file read through a ContentSource, so Explorer copies them
// with its own progress and never waits on Osdrop's thread (IDataObjectAsyncCapability). Drags
// up to a size cap are also offered as CF_HDROP to copies staged on request, for apps that take
// no virtual files.

#include <osdrop/win/drag_files.hpp>

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace osdrop::engine::drag {

enum class DragOutcome : std::uint8_t {
    Dropped,   ///< a target took the files
    Cancelled, ///< Escape, or a place that takes nothing
    Failed,    ///< the drag could not start: no button press came, or COM refused
};

struct DragRequest {
    POINT at{};                                ///< physical pixels
    std::vector<win::drag::TreeEntry> entries; ///< every file and folder, parents first
    std::shared_ptr<win::drag::ContentSource> source;
    std::uint64_t staged_limit = 64ULL * 1024 * 1024; ///< CF_HDROP only up to this total
};

/// Starts one drag on a thread of its own; `done` runs on that thread once the drag ended and
/// the target finished reading. Returns false when the thread could not start.
bool start_drag(DragRequest request, std::function<void(DragOutcome)> done);

} // namespace osdrop::engine::drag
