#pragma once

// The engine's link to OsdropInput (DEVPLAN Phases 3 and 11). While a member is connected, this
// PC's own input is watched and handed to core, which decides crossings; while the cursor is on
// another computer, the input is captured here and still goes to core, confirmed by a heartbeat so
// a hung engine never keeps it. Input another computer sends is injected through the same pipe.
// Requests go out from a thread of the bridge's own, so core's thread never waits on the pipe;
// reports come in on the pipe's reader thread and go straight to core. While OsdropInput cannot be
// reached (the service is not installed, or not running yet), this PC tells the others it cannot
// be controlled, and tries again every few seconds. Someone using this PC's own mouse or keyboard
// during control takes it back, and a cursor moved by something else here is reported, so the
// controller's cursor follows.

#include <osdrop/osdrop.h>
#include <osdrop/sys/input_client.hpp>
#include <osdrop/win/forwarder.hpp>
#include <osdrop/win/group_engine.hpp>
#include <osdrop/win/injection.hpp>
#include <osdrop/win/screens.hpp>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>
#include <vector>

namespace osdrop::engine {

class InputBridge {
public:
    /// What the shell shows; called on the bridge's threads, so they must return quickly.
    struct Listener {
        std::function<void(bool reachable)> reachable; ///< whether OsdropInput answers
        std::function<void(bool locked)> locked;       ///< "lock input here"
    };

    InputBridge(win::GroupEngine& group, Listener listener);
    ~InputBridge();
    InputBridge(const InputBridge&) = delete;
    InputBridge& operator=(const InputBridge&) = delete;
    InputBridge(InputBridge&&) = delete;
    InputBridge& operator=(InputBridge&&) = delete;

    // From core's thread; they only queue.
    void controlled(bool controlled);
    void inject(const osdrop_input& input);
    void capture_began(const std::vector<osdrop_input>& release_here);
    void capture_ended(const win::CaptureEnd& end);

    /// Whether a member is connected: this PC's input is watched only then. Any thread.
    void set_watching(bool watching);
    /// This PC's monitors, which a watched cursor stays on. Any thread.
    void set_monitors(std::vector<win::PhysicalMonitor> monitors);
    /// "Lock input here", from the shell or the lock hotkey. Any thread.
    void set_locked(bool locked);

    /// Ends the bridge's thread; what arrives later is dropped. Before the group engine goes.
    void stop();

private:
    using Clock = std::chrono::steady_clock;
    struct Began {
        std::vector<osdrop_input> release_here;
    };
    using CaptureChange = std::variant<Began, win::CaptureEnd>;

    struct Work {
        std::vector<osdrop_input> inputs;
        std::optional<bool> control;
        std::vector<CaptureChange> captures;
        bool watching = false;
        bool stopping = false;
    };

    void run();
    [[nodiscard]] Work take_work();
    void connect();
    void apply(const Work& work);
    void send_all(const std::vector<osdrop_input>& inputs);
    void begin_capture(const Began& began);
    void end_capture(const win::CaptureEnd& end);
    void watch(bool watching);
    void tick();
    void watch_cursor();
    void report_caps_lock();

    // The pipe's reader thread.
    void received(const win::input::Report& report);
    void local_input();
    void hotkey(win::input::Hotkey hotkey);
    [[nodiscard]] std::uint64_t age_us(std::uint64_t ticks) const;

    win::GroupEngine& group_;
    const Listener listener_;
    const LARGE_INTEGER frequency_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<osdrop_input> pending_;
    std::optional<bool> control_change_; // the newest control state not passed on yet
    std::vector<CaptureChange> capture_changes_;
    bool watching_wanted_ = false;
    bool watching_changed_ = false;
    bool stopping_ = false;

    // The forwarder turns reports into core's input on the reader thread; captures move it.
    std::mutex forwarder_mutex_;
    win::InputForwarder forwarder_;

    // Read on the reader thread.
    std::atomic<bool> capturing_{false};
    std::atomic<bool> closed_{false};
    std::atomic<bool> locked_{false}; // "lock input here"

    // The bridge's thread only.
    std::unique_ptr<sys::InputClient> client_;
    std::optional<bool> reachable_; // as last told to core
    bool controlled_ = false;
    bool watching_ = false; // as last told to OsdropInput
    win::Injection injection_;
    POINT expected_{};                // where the last injected move put the cursor
    std::optional<POINT> moved_here_; // a different position seen once, waiting for a second look
    Clock::time_point last_heartbeat_;
    Clock::time_point last_cursor_check_;
    Clock::time_point last_caps_check_;
    Clock::time_point caps_settled_; // Caps Lock set by Osdrop is read again only after this
    std::optional<bool> caps_reported_;

    std::thread thread_; // declared last: starts once everything above exists
};

} // namespace osdrop::engine
