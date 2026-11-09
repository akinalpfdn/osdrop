// OsdropInput: captures and injects mouse and keyboard input as SYSTEM inside the user's session,
// so it works over elevated windows too (DEVPLAN Phase 3). OsdropService starts it in the active
// console session and asks it to quit through an event; the pipe is its only other way in.
#include "cursors.hpp"
#include "hooks.hpp"
#include "injector.hpp"
#include "pipe_server.hpp"

#include <osdrop/sys/file_log.hpp>
#include <osdrop/sys/handles.hpp>
#include <osdrop/sys/session.hpp>
#include <osdrop/win/input_session.hpp>
#include <osdrop/win/report_queue.hpp>

#include <windows.h>

#include <filesystem>
#include <string>

namespace {

std::filesystem::path cursor_marker(DWORD session) {
    std::wstring program_data(MAX_PATH, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"ProgramData", program_data.data(),
                                                 static_cast<DWORD>(program_data.size()));
    program_data.resize(length < program_data.size() ? length : 0);
    return std::filesystem::path(program_data) / L"Osdrop" /
           (L"cursors-hidden." + std::to_wstring(session));
}

// A crash with the cursors hidden would leave the user without one until the next start.
LONG WINAPI restore_cursors_on_crash(EXCEPTION_POINTERS* /*exception*/) {
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
    return EXCEPTION_CONTINUE_SEARCH;
}

int run() {
    using namespace osdrop;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetUnhandledExceptionFilter(restore_cursors_on_crash);
    sys::FileLog log(L"input");
    const DWORD session = sys::current_session();
    const std::filesystem::path marker = cursor_marker(session);
    input::CursorHider::recover(marker);

    // Started by the service, it quits when the service sets this; started by hand, never.
    sys::UniqueHandle stop(
        OpenEventW(SYNCHRONIZE, FALSE, sys::input_stop_event_name(session).c_str()));
    if (!stop) {
        stop = sys::make_event(true);
    }
    const sys::UniqueHandle reports_ready = sys::make_event(false);
    if (!stop || !reports_ready) {
        log.error("Cannot create events");
        return 1;
    }
    win::input::ReportQueue reports([event = reports_ready.get()] { SetEvent(event); });
    input::Hooks hooks(reports);
    if (!hooks.start()) {
        log.error("Cannot install the input hooks: " + std::to_string(GetLastError()));
        return 1;
    }
    input::CursorHider cursors(marker);
    input::Injector injector(hooks, cursors);
    win::input::InputSession client(injector, log);
    log.info("Started in session " + std::to_string(session));

    input::PipeServer server(client, reports, reports_ready.get(), log, stop.get());
    const bool served = server.run();
    client.reset();
    log.info("Stopped");
    return served ? 0 : 1;
}

} // namespace

// Entry point: the signature is fixed by the C runtime.
// NOLINTNEXTLINE(misc-use-internal-linkage,readability-non-const-parameter)
int WINAPI wWinMain(HINSTANCE /*instance*/, HINSTANCE /*previous*/, PWSTR /*command_line*/,
                    int /*show*/) {
    return run();
}
