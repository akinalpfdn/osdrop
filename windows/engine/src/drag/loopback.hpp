#pragma once

// The drag bridge's developer loopback (DEVPLAN Phase 4, debug builds of the shell): a drag
// dropped on a drop target at one of the PC's edges comes back as an Osdrop drag at the left edge
// of the same monitor, carrying the same files as virtual files. The button press that starts it
// is injected; the drag then follows the mouse and drops where the button is clicked. It is the
// go/no-go check that both halves work on one PC before drags cross to other computers.

#include "drag/edge_targets.hpp"

#include <osdrop/win/screens.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace osdrop::engine::drag {

class Loopback {
public:
    using Log = std::function<void(const char* message)>;

    explicit Loopback(Log log);

    /// On or off; the strips line the monitors' outer edges. Any thread.
    void set_enabled(bool enabled);
    void set_monitors(std::vector<win::PhysicalMonitor> monitors);

private:
    void update();
    void dropped(const std::vector<std::filesystem::path>& items, POINT at);

    [[nodiscard]] std::vector<win::PhysicalMonitor> monitors();

    Log log_;
    // Two locks: a drop runs on the targets' thread and reads the monitors, while turning the
    // loopback off waits for that thread under mutex_.
    std::mutex mutex_;
    bool enabled_ = false;                 ///< under mutex_
    std::unique_ptr<EdgeTargets> targets_; ///< under mutex_
    std::mutex monitors_mutex_;
    std::vector<win::PhysicalMonitor> monitors_; ///< under monitors_mutex_
};

} // namespace osdrop::engine::drag
