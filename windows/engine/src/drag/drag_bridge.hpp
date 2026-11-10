#pragma once

// The PC's part of drags across computers (DEVPLAN Phase 14; docs/PROTOCOL.md, "Drag messages").
// Core decides where a drag goes; this bridge reports the drags that reach the PC's edges, keeps
// such a drag where it waits while the cursor is away, ends it there once its outcome is known,
// and starts the drags that arrive from other computers, their files read as they arrive.
//
// As the source: a drag reaching an edge strip is reported with its files (or as promised when it
// carries only virtual files). When the cursor crosses with it, the input bridge parks the cursor;
// a hold window waits at the park point first, so the drag stays on Osdrop's target. Its outcome
// releases the left button there: a copy with nothing read when it was dropped elsewhere, refused
// otherwise, or its virtual files saved and reported when another computer wants them. A drag
// started under remote control waits at the edge the same way; the input bridge keeps its button
// down past the control.
//
// As the target: a drag that arrives is received at once into a staging folder, started under the
// cursor from an injected press, withdrawn by cancelling it, and its outcome reported as soon as
// it ends. Staging is removed once the target finished reading, except where an app was given the
// files' paths: those stay until Osdrop starts again.
//
// Core's events only queue work; a thread of the bridge's own calls the group engine.

#include "drag/drag_starter.hpp"
#include "drag/edge_targets.hpp"

#include <osdrop/osdrop.h>
#include <osdrop/win/group_engine.hpp>
#include <osdrop/win/network_drag.hpp>
#include <osdrop/win/screens.hpp>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace osdrop::engine {
class InputBridge;
}

namespace osdrop::engine::drag {

class DragBridge {
public:
    using Log = std::function<void(const char* message)>;

    /// `staging`: where arriving drags are received, emptied now.
    DragBridge(win::GroupEngine& group, InputBridge& input, std::filesystem::path staging, Log log);
    ~DragBridge();
    DragBridge(const DragBridge&) = delete;
    DragBridge& operator=(const DragBridge&) = delete;
    DragBridge(DragBridge&&) = delete;
    DragBridge& operator=(DragBridge&&) = delete;

    /// Whether a member is connected: the edges take drags only then. Any thread.
    void set_active(bool active);
    /// Whether the developer loopback has the edges. Any thread.
    void set_paused(bool paused);
    void set_monitors(std::vector<win::PhysicalMonitor> monitors);

    // Core's events, on its thread: they only queue.
    void arrived(const win::DragArrival& arrival);
    void withdrawn(std::uint32_t drag_id);
    void ended(std::uint32_t drag_id, osdrop_drag_outcome outcome);
    void wanted();
    /// The cursor came back: a drag of this PC goes on from the edge.
    void capture_ended();

    /// From the input bridge's thread, before it parks the cursor for a capture: a drag of this
    /// PC that the cursor carries away gets the hold window there first.
    void parking(POINT park);

    /// Ends the bridge's thread and every drag. Before the group engine and the input bridge go.
    void stop();

private:
    using Clock = std::chrono::steady_clock;
    struct Arrival {
        std::uint32_t drag_id = 0;
        std::shared_ptr<win::drag::NetworkDrag> network;
        /// An app was given the received files' paths: they stay after the drag.
        std::shared_ptr<std::atomic<bool>> paths_given;
        std::shared_ptr<DragHandle> handle;
        std::filesystem::path staging;
    };

    void post(std::function<void()> task);
    void post_after(Clock::duration delay, std::function<void()> task);
    void run();
    void update_strips();

    // On the bridge's thread.
    void report(std::vector<std::filesystem::path> files, bool virtual_files);
    void release();
    void take_ended(const std::vector<std::filesystem::path>& items);
    void end_taking(const std::vector<std::filesystem::path>& items);
    void arrive(const win::DragArrival& arrival);
    void arrival_ended(std::uint32_t drag_id, DragOutcome outcome);
    void arrival_done(const std::shared_ptr<Arrival>& arrival);

    win::GroupEngine& group_;
    InputBridge& input_;
    const std::filesystem::path staging_;
    const Log log_;

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> tasks_;                                 ///< under mutex_
    std::vector<std::pair<Clock::time_point, std::function<void()>>> timers_; ///< under mutex_
    bool stopping_ = false;                                                   ///< under mutex_
    bool active_ = false;                                                     ///< under mutex_
    bool paused_ = false;                                                     ///< under mutex_
    std::vector<win::PhysicalMonitor> monitors_;                              ///< under mutex_

    std::atomic<DropTaking> taking_{DropTaking::Refuse}; ///< read at a drop on the targets
    std::atomic<bool> reported_{false};                  ///< a drag of this PC is reported

    // The bridge's thread only.
    bool taking_files_ = false; ///< wanted: the drop saves its files
    std::uint64_t take_attempt_ = 0;
    std::shared_ptr<Arrival> arrival_;         ///< the drag started here for another computer's
    std::vector<std::filesystem::path> given_; ///< staging an app was given paths into

    std::unique_ptr<EdgeTargets> targets_; // after the state its handler reaches
    std::thread thread_;                   // declared last: starts once everything above exists
};

} // namespace osdrop::engine::drag
