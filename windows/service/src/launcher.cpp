#include "launcher.hpp"

#include <osdrop/sys/session.hpp>

#include <wtsapi32.h>

#include <algorithm>
#include <array>
#include <utility>

namespace osdrop::service {
namespace {

constexpr DWORD kNoSession = 0xFFFFFFFF;
constexpr DWORD kNobodyRetryMilliseconds = 2000;
constexpr DWORD kQuitTimeoutMilliseconds = 3000;
constexpr DWORD kFirstBackoffMilliseconds = 500;
constexpr DWORD kMaxBackoffMilliseconds = 30'000;
/// A run this long counts as healthy: the next failure starts again at once.
constexpr ULONGLONG kHealthyRunMilliseconds = 60'000;

/// This service's own token, with the session changed: OsdropInput runs as SYSTEM in `session`.
sys::UniqueHandle system_token_for(DWORD session) {
    HANDLE own = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY |
                              TOKEN_ADJUST_SESSIONID | TOKEN_ADJUST_DEFAULT,
                          &own)) {
        return {};
    }
    const sys::UniqueHandle self(own);
    HANDLE duplicate = nullptr;
    if (!DuplicateTokenEx(self.get(), MAXIMUM_ALLOWED, nullptr, SecurityIdentification,
                          TokenPrimary, &duplicate)) {
        return {};
    }
    sys::UniqueHandle token(duplicate);
    DWORD target = session;
    if (!SetTokenInformation(token.get(), TokenSessionId, &target, sizeof(target))) {
        return {};
    }
    return token;
}

} // namespace

InputLauncher::InputLauncher(sys::FileLog& log, HANDLE stop, HANDLE session_changed)
    : log_(log), stop_(stop), session_changed_(session_changed) {
    if (const auto folder = sys::executable_folder()) {
        executable_ = *folder + L"\\OsdropInput.exe";
    }
}

void InputLauncher::run() {
    DWORD backoff = 0;
    while (WaitForSingleObject(stop_, 0) == WAIT_TIMEOUT) {
        ResetEvent(session_changed_);
        const DWORD session = WTSGetActiveConsoleSessionId();
        if (session == kNoSession || !sys::session_user_sid(session)) {
            if (!pause(kNobodyRetryMilliseconds)) {
                return; // nobody is logged on at the console yet
            }
            continue;
        }
        Running running;
        const Outcome outcome = launch(session, running) ? watch(running) : Outcome::Ended;
        if (outcome == Outcome::Stopping) {
            return;
        }
        if (outcome == Outcome::Moved) {
            log_.info("The console session changed; OsdropInput follows it");
            backoff = 0;
            continue;
        }
        backoff = next_pause(backoff, running);
        if (backoff != 0 && !pause(backoff)) {
            return;
        }
    }
}

InputLauncher::Outcome InputLauncher::watch(Running& running) {
    const std::array<HANDLE, 3> handles{running.process.get(), stop_, session_changed_};
    while (true) {
        const DWORD signaled = WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                                      handles.data(), FALSE, INFINITE);
        if (signaled == WAIT_OBJECT_0) {
            return Outcome::Ended;
        }
        if (signaled == WAIT_OBJECT_0 + 2) {
            ResetEvent(session_changed_);
            if (WTSGetActiveConsoleSessionId() == running.session) {
                continue; // a change in another session, or a lock: it stays
            }
            end(running);
            return Outcome::Moved;
        }
        end(running);
        return Outcome::Stopping;
    }
}

DWORD InputLauncher::next_pause(DWORD backoff, const Running& running) {
    if (running.process) {
        DWORD code = 0;
        GetExitCodeProcess(running.process.get(), &code);
        log_.warning("OsdropInput ended with code " + std::to_string(code));
        // After a healthy run it starts again at once, so the user's input comes back at once.
        if (GetTickCount64() - running.started_ms >= kHealthyRunMilliseconds) {
            return 0;
        }
    }
    // While it keeps failing, the pauses grow.
    return backoff == 0 ? kFirstBackoffMilliseconds
                        : std::min(backoff * 2, kMaxBackoffMilliseconds);
}

bool InputLauncher::launch(DWORD session, Running& running) {
    if (executable_.empty()) {
        log_.error("Cannot find this service's folder");
        return false;
    }
    // Created before OsdropInput opens it: only SYSTEM and administrators may make Global names.
    running.quit.reset(
        CreateEventW(nullptr, TRUE, FALSE, sys::input_stop_event_name(session).c_str()));
    if (!running.quit) {
        log_.error("Cannot create the quit event: " + std::to_string(GetLastError()));
        return false;
    }
    ResetEvent(running.quit.get());
    const sys::UniqueHandle token = system_token_for(session);
    if (!token) {
        log_.error("Cannot make a token for session " + std::to_string(session) + ": " +
                   std::to_string(GetLastError()));
        return false;
    }
    std::wstring desktop = L"winsta0\\default";
    std::wstring command = L"\"" + executable_ + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.lpDesktop = desktop.data();
    PROCESS_INFORMATION process{};
    if (!CreateProcessAsUserW(token.get(), executable_.c_str(), command.data(), nullptr, nullptr,
                              FALSE, ABOVE_NORMAL_PRIORITY_CLASS | CREATE_UNICODE_ENVIRONMENT,
                              nullptr, nullptr, &startup, &process)) {
        log_.error("Cannot start OsdropInput in session " + std::to_string(session) + ": " +
                   std::to_string(GetLastError()));
        return false;
    }
    CloseHandle(process.hThread);
    running.process.reset(process.hProcess);
    running.session = session;
    running.started_ms = GetTickCount64();
    log_.info("Started OsdropInput in session " + std::to_string(session));
    return true;
}

void InputLauncher::end(Running& running) {
    SetEvent(running.quit.get());
    if (WaitForSingleObject(running.process.get(), kQuitTimeoutMilliseconds) != WAIT_OBJECT_0) {
        log_.warning("OsdropInput did not quit; ending it");
        TerminateProcess(running.process.get(), 1);
        WaitForSingleObject(running.process.get(), kQuitTimeoutMilliseconds);
    }
}

bool InputLauncher::pause(DWORD milliseconds) {
    const std::array<HANDLE, 2> handles{stop_, session_changed_};
    return WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE,
                                  milliseconds) != WAIT_OBJECT_0;
}

} // namespace osdrop::service
