#pragma once

// Keeps OsdropInput running in the active console session (DEVPLAN Phase 3): it starts there with
// this service's SYSTEM token on winsta0\default, starts again when it ends (at once the first
// time, then with growing pauses while it keeps failing), and moves when the console session
// changes (log on, log off, fast user switching). OsdropInput is asked to quit through an event
// before it is ended.

#include <osdrop/sys/file_log.hpp>
#include <osdrop/sys/handles.hpp>

#include <windows.h>

#include <cstdint>
#include <string>

namespace osdrop::service {

class InputLauncher {
public:
    InputLauncher(sys::FileLog& log, HANDLE stop, HANDLE session_changed);
    ~InputLauncher() = default;
    InputLauncher(const InputLauncher&) = delete;
    InputLauncher& operator=(const InputLauncher&) = delete;
    InputLauncher(InputLauncher&&) = delete;
    InputLauncher& operator=(InputLauncher&&) = delete;

    /// Runs until `stop` is set; OsdropInput is ended before it returns.
    void run();

private:
    struct Running {
        sys::UniqueHandle process;
        sys::UniqueHandle quit;
        DWORD session = 0;
        ULONGLONG started_ms = 0;
    };

    enum class Outcome : std::uint8_t { Ended, Moved, Stopping };

    [[nodiscard]] bool launch(DWORD session, Running& running);
    /// Waits for OsdropInput to end, the console session to move or the service to stop.
    [[nodiscard]] Outcome watch(Running& running);
    /// The pause before the next start, after `running` ended or did not start.
    [[nodiscard]] DWORD next_pause(DWORD backoff, const Running& running);
    void end(Running& running);
    /// Waits for `stop` or `session_changed` up to `milliseconds`; false when stopping.
    [[nodiscard]] bool pause(DWORD milliseconds);

    sys::FileLog& log_;
    HANDLE stop_;
    HANDLE session_changed_;
    std::wstring executable_;
};

} // namespace osdrop::service
