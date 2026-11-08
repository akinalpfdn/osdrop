#pragma once

// The engine's link to OsdropInput (DEVPLAN Phase 3). Input another computer sends is injected
// through the input pipe from a thread of the bridge's own, so core's thread never waits on the
// pipe. While OsdropInput cannot be reached (the service is not installed, or not running yet),
// this PC tells the others it cannot be controlled, and tries again every few seconds. Someone
// using this PC's own mouse or keyboard during control takes it back, and a cursor moved by
// something else here is reported, so the controller's cursor follows.

#include <osdrop/osdrop.h>
#include <osdrop/sys/input_client.hpp>
#include <osdrop/win/group_engine.hpp>
#include <osdrop/win/injection.hpp>

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace osdrop::engine {

class InputBridge {
public:
    explicit InputBridge(win::GroupEngine& group);
    ~InputBridge();
    InputBridge(const InputBridge&) = delete;
    InputBridge& operator=(const InputBridge&) = delete;
    InputBridge(InputBridge&&) = delete;
    InputBridge& operator=(InputBridge&&) = delete;

    // From core's thread; they only queue.
    void controlled(bool controlled);
    void inject(const osdrop_input& input);

    /// Ends the bridge's thread; what arrives later is dropped. Before the group engine goes.
    void stop();

private:
    struct Work {
        std::vector<osdrop_input> inputs;
        std::optional<bool> control;
        bool stopping = false;
    };

    void run();
    [[nodiscard]] Work take_work();
    void connect();
    void apply(const Work& work);
    void send_all(const std::vector<osdrop_input>& inputs);
    void watch_cursor();
    void local_input();

    win::GroupEngine& group_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<osdrop_input> pending_;
    std::optional<bool> control_change_; // the newest control state not passed on yet
    bool stopping_ = false;

    // The bridge's thread only.
    std::unique_ptr<sys::InputClient> client_;
    std::atomic<bool> closed_{false};
    std::optional<bool> reachable_; // as last told to core
    bool controlled_ = false;
    win::Injection injection_;
    POINT expected_{};                // where the last injected move put the cursor
    std::optional<POINT> moved_here_; // a different position seen once, waiting for a second look

    std::thread thread_; // declared last: starts once everything above exists
};

} // namespace osdrop::engine
