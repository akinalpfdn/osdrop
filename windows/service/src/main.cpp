// OsdropService: LocalSystem service that keeps OsdropInput running in the active console
// session (DEVPLAN Phase 3).
//   OsdropService.exe --install     register the auto-start service, or point an existing one at
//                                   this executable, and start it (administrator)
//   OsdropService.exe --stop        stop the service so an upgrade can replace its files
//   OsdropService.exe --uninstall   stop and remove the service
//   OsdropService.exe               entry point used by the Service Control Manager
#include "installer.hpp"
#include "service_host.hpp"

#include <windows.h>

#include <iostream>
#include <string_view>

// Entry point: the signature is fixed by the C runtime.
// NOLINTNEXTLINE(misc-use-internal-linkage,*-avoid-c-arrays)
int wmain(int argc, wchar_t* argv[]) {
    const std::wstring_view command = argc == 2 ? std::wstring_view(argv[1]) : std::wstring_view();
    if (command == L"--install") {
        return osdrop::service::install_service();
    }
    if (command == L"--stop") {
        return osdrop::service::stop_service();
    }
    if (command == L"--uninstall") {
        return osdrop::service::uninstall_service();
    }
    if (argc == 1) {
        if (osdrop::service::run_as_service()) {
            return 0;
        }
        if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            std::cerr << "OsdropService runs as a Windows service. Use --install or --uninstall.\n";
            return 2;
        }
        return static_cast<int>(GetLastError());
    }
    std::cerr << "Usage: OsdropService.exe [--install | --stop | --uninstall]\n";
    return 2;
}
