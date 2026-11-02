// OsdropInput: captures and injects mouse and keyboard input as SYSTEM inside the user's session,
// started by OsdropService. The hook thread, cursor parking, injection and the pipe server arrive
// in Phase 3.
#include <windows.h>

// Entry point: the signature is fixed by the C runtime.
// NOLINTNEXTLINE(misc-use-internal-linkage,readability-non-const-parameter)
int WINAPI wWinMain(HINSTANCE /*instance*/, HINSTANCE /*previous*/, PWSTR /*command_line*/,
                    int /*show*/) {
    return 0;
}
