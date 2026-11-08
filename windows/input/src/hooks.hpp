#pragma once

// The low-level mouse and keyboard hooks (DEVPLAN Phase 3). They run on a thread of their own with
// its own message loop, and the callbacks only decide and enqueue: Windows removes a hook whose
// callback is slow. While captured, this PC's input is swallowed and reported; Osdrop's own
// injection (tagged in dwExtraInfo) always passes.

#include <osdrop/win/hook_timings.hpp>
#include <osdrop/win/report_queue.hpp>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace osdrop::input {

/// dwExtraInfo of every event OsdropInput injects, so its hooks let them through.
inline constexpr ULONG_PTR kInjectedTag = 0x4F53'4450; // "OSDP"

class Hooks {
public:
    explicit Hooks(win::input::ReportQueue& reports);
    ~Hooks();
    Hooks(const Hooks&) = delete;
    Hooks& operator=(const Hooks&) = delete;
    Hooks(Hooks&&) = delete;
    Hooks& operator=(Hooks&&) = delete;

    /// Installs both hooks on the hook thread; false when either could not be installed.
    [[nodiscard]] bool start();

    /// From now on this PC's input is swallowed and reported as movement from `park`, where the
    /// cursor waits.
    void capture(POINT park);
    void release();
    void set_controlled(bool controlled);
    [[nodiscard]] win::input::Stats stats() const { return timings_.stats(); }

private:
    static LRESULT CALLBACK on_mouse(int code, WPARAM message, LPARAM data);
    static LRESULT CALLBACK on_keyboard(int code, WPARAM message, LPARAM data);
    [[nodiscard]] bool mouse(WPARAM message, const MSLLHOOKSTRUCT& event);
    [[nodiscard]] bool keyboard(const KBDLLHOOKSTRUCT& event);
    void local_input();
    void record(LARGE_INTEGER started);
    void run();

    win::input::ReportQueue& reports_;
    win::input::HookTimings timings_;
    LARGE_INTEGER frequency_{};
    std::atomic<bool> capturing_{false};
    std::atomic<bool> controlled_{false};
    std::atomic<std::int64_t> park_{0}; // x in the high half, y in the low one
    std::atomic<std::int64_t> last_local_ms_{0};
    std::atomic<DWORD> thread_id_{0};
    std::atomic<int> installed_{-1}; // -1 while starting, then 0 or 1
    std::thread thread_;
};

} // namespace osdrop::input
