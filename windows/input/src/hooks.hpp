#pragma once

// The low-level mouse and keyboard hooks (DEVPLAN Phases 3 and 11). They run on a thread of their
// own with its own message loop, and the callbacks only decide and enqueue: Windows removes a hook
// whose callback is slow. While watched, this PC's input is reported as it acts here; while
// captured, it is swallowed and reported; while another computer controls this PC, its use is
// reported as LocalInput. Osdrop's own injection (tagged in dwExtraInfo) always passes unreported.
//
// Movement is measured from where the cursor is when the hook runs: a hook sees where Windows takes
// the cursor before it keeps it on a monitor, so a push against an outer edge still moves.

#include <osdrop/win/chords.hpp>
#include <osdrop/win/hook_timings.hpp>
#include <osdrop/win/report_queue.hpp>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

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

    /// From now on this PC's input is swallowed and reported.
    void capture();
    void release();
    void set_controlled(bool controlled);
    void set_watching(bool watching);
    /// The chords from now on; the hook thread takes them between two events.
    void set_chords(const win::input::Chord& return_home, const win::input::Chord& lock_input);
    /// Osdrop moves the cursor itself now: movement measured in the next moments may start from
    /// where the cursor was before, and is not reported.
    void warping();
    /// Whether this PC's own left button is down, as the hook last saw it, in every state
    /// (captured too). Any thread.
    [[nodiscard]] bool left_held() const { return left_held_.load(); }
    [[nodiscard]] win::input::Stats stats() const { return timings_.stats(); }

private:
    static LRESULT CALLBACK on_mouse(int code, WPARAM message, LPARAM data);
    static LRESULT CALLBACK on_keyboard(int code, WPARAM message, LPARAM data);
    [[nodiscard]] bool mouse(WPARAM message, const MSLLHOOKSTRUCT& event, std::int64_t ticks);
    [[nodiscard]] bool keyboard(const KBDLLHOOKSTRUCT& event);
    /// Whether a chord took the key: then it never reaches an app.
    [[nodiscard]] bool chord(win::ScanCode scan, bool down, bool capturing);
    void report(WPARAM message, const MSLLHOOKSTRUCT& event, std::int64_t ticks, bool captured);
    void report_motion(POINT to, std::int64_t ticks, bool captured);
    void local_input();
    void record(LARGE_INTEGER started);
    void apply_chords();
    void run();

    win::input::ReportQueue& reports_;
    win::input::HookTimings timings_;
    win::input::ChordMatcher chords_; // the hook thread only
    std::mutex chords_mutex_;
    std::optional<std::pair<win::input::Chord, win::input::Chord>> new_chords_; // chords_mutex_
    LARGE_INTEGER frequency_{};
    std::atomic<bool> capturing_{false};
    std::atomic<bool> controlled_{false};
    std::atomic<bool> watching_{false};
    std::atomic<bool> left_held_{false};
    std::atomic<std::int64_t> warped_{0}; // QueryPerformanceCounter when Osdrop last moved it
    std::atomic<std::int64_t> last_local_ms_{0};
    std::atomic<DWORD> thread_id_{0};
    std::atomic<int> installed_{-1}; // -1 while starting, then 0 or 1
    std::thread thread_;
};

} // namespace osdrop::input
