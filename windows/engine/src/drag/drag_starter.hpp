#pragma once

// Starting a drag on the PC from injected input (DEVPLAN Phases 4 and 14). A small, nearly
// invisible window waits under a point; the left button pressed there (by the user's hand or
// injected) starts DoDragDrop on the window's own STA thread, and the moves that follow carry the
// drag to the drop. The data object offers the files as virtual files: a FILEGROUPDESCRIPTORW for
// every file and folder, and a stream per file read through the drag's content, so Explorer copies
// them with its own progress and never waits on Osdrop's thread (IDataObjectAsyncCapability).
// The content may still be on its way (a drag from another computer): the files are described
// when a target first asks, and a stream reads a file as it arrives. Drags up to a size cap are
// also offered as CF_HDROP to the content's local files, for apps that take no virtual files.

#include <osdrop/win/drag_files.hpp>
#include <osdrop/win/network_drag.hpp>

#include <windows.h>

#include <ole2.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace osdrop::engine::drag {

enum class DragOutcome : std::uint8_t {
    Dropped,   ///< a target took the files
    Cancelled, ///< Escape, cancel(), or a place that takes nothing
    Failed,    ///< the drag could not start: no button press came, or COM refused
};

/// What a drag Osdrop starts carries.
class DragContent : public win::drag::ContentSource {
public:
    /// Every file and folder, parents first; may wait until they are known. Nullopt when they
    /// cannot be had.
    [[nodiscard]] virtual std::optional<std::vector<win::drag::TreeEntry>> entries() = 0;
    /// The top-level items as files on this PC, for apps that take only paths; may wait until
    /// they are there. Nullopt when they cannot be had.
    [[nodiscard]] virtual std::optional<std::vector<std::filesystem::path>> local_items() = 0;
};

struct DragRequest {
    POINT at{}; ///< physical pixels
    /// The top-level items: the drag image, and the size the CF_HDROP cap is measured against.
    std::vector<win::drag::ArrivingItem> items;
    std::shared_ptr<DragContent> content;
    std::uint64_t staged_limit = 64ULL * 1024 * 1024; ///< CF_HDROP only up to this total
};

/// A drag that was started; cancel() ends it without a drop, before or after the press.
class DragHandle {
public:
    /// Any thread.
    void cancel();
    [[nodiscard]] bool cancelled() const { return cancelled_.load(); }
    /// The drag's thread, once its message queue exists.
    void attach(DWORD thread);

private:
    std::atomic<bool> cancelled_{false};
    std::atomic<DWORD> thread_{0};
};

/// Starts one drag on a thread of its own. `ended` runs on that thread as soon as the drag ended,
/// `done` once the target also finished reading and everything was released. Null when the
/// thread could not start.
[[nodiscard]] std::shared_ptr<DragHandle> start_drag(DragRequest request,
                                                     std::function<void(DragOutcome)> ended,
                                                     std::function<void()> done = {});

/// A format every drag Osdrop starts carries, so Osdrop's own drop targets leave it alone.
[[nodiscard]] CLIPFORMAT own_drag_format();

} // namespace osdrop::engine::drag
