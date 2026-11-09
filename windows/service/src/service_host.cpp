#include "service_host.hpp"

#include "launcher.hpp"
#include "service_constants.hpp"

#include <osdrop/sys/file_log.hpp>
#include <osdrop/sys/handles.hpp>

#include <windows.h>

#include <wtsapi32.h>

#include <array>
#include <string>

namespace osdrop::service {
namespace {

// The SCM calls plain C callbacks, so the running service's state lives here.
struct ServiceState {
    SERVICE_STATUS_HANDLE status_handle = nullptr;
    SERVICE_STATUS status{};
    sys::UniqueHandle stop;
    sys::UniqueHandle session_changed;
    const char* stop_reason = "unknown";
};
ServiceState g_state; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void report(DWORD state, DWORD wait_hint = 0) {
    g_state.status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_state.status.dwCurrentState = state;
    g_state.status.dwControlsAccepted =
        state == SERVICE_RUNNING
            ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_SESSIONCHANGE
            : 0;
    g_state.status.dwWin32ExitCode = NO_ERROR;
    g_state.status.dwWaitHint = wait_hint;
    SetServiceStatus(g_state.status_handle, &g_state.status);
}

DWORD WINAPI control_handler(DWORD control, DWORD event_type, LPVOID /*event_data*/,
                             LPVOID /*context*/) {
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        g_state.stop_reason =
            control == SERVICE_CONTROL_STOP ? "stop requested" : "system shutdown";
        report(SERVICE_STOP_PENDING, kServiceStopTimeoutMilliseconds);
        SetEvent(g_state.stop.get());
        return NO_ERROR;
    case SERVICE_CONTROL_SESSIONCHANGE:
        // OsdropInput lives in the console session; it follows log on, log off and switching.
        if (event_type == WTS_CONSOLE_CONNECT || event_type == WTS_CONSOLE_DISCONNECT ||
            event_type == WTS_SESSION_LOGON || event_type == WTS_SESSION_LOGOFF) {
            SetEvent(g_state.session_changed.get());
        }
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI service_main(DWORD /*argc*/, LPWSTR* /*argv*/) {
    g_state.status_handle = RegisterServiceCtrlHandlerExW(kServiceName, control_handler, nullptr);
    if (g_state.status_handle == nullptr) {
        return;
    }
    report(SERVICE_START_PENDING, kServiceStopTimeoutMilliseconds);
    g_state.stop = sys::make_event(true);
    g_state.session_changed = sys::make_event(true);
    if (!g_state.stop || !g_state.session_changed) {
        report(SERVICE_STOPPED);
        return;
    }
    sys::FileLog log(L"service");
    log.info("Service starting");
    report(SERVICE_RUNNING);

    InputLauncher launcher(log, g_state.stop.get(), g_state.session_changed.get());
    launcher.run();

    log.info(std::string("Service stopped: ") + g_state.stop_reason);
    report(SERVICE_STOPPED);
}

} // namespace

bool run_as_service() {
    // The SCM takes a mutable name that must stay alive while the dispatcher runs.
    static std::wstring name(kServiceName);
    const std::array<SERVICE_TABLE_ENTRYW, 2> table{{
        {.lpServiceName = name.data(), .lpServiceProc = service_main},
        {.lpServiceName = nullptr, .lpServiceProc = nullptr},
    }};
    return StartServiceCtrlDispatcherW(table.data()) != FALSE;
}

} // namespace osdrop::service
