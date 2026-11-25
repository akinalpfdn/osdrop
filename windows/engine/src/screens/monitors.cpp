#include "screens/monitors.hpp"

#include <windows.h>

#include <shellscalingapi.h>

#include <map>
#include <string>
#include <vector>

namespace osdrop::engine {
namespace {

/// Physical pixels for everything this thread asks, whatever the process declares.
class PhysicalPixelsScope {
public:
    PhysicalPixelsScope()
        : previous_(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
    ~PhysicalPixelsScope() {
        if (previous_ != nullptr) {
            SetThreadDpiAwarenessContext(previous_);
        }
    }
    PhysicalPixelsScope(const PhysicalPixelsScope&) = delete;
    PhysicalPixelsScope& operator=(const PhysicalPixelsScope&) = delete;
    PhysicalPixelsScope(PhysicalPixelsScope&&) = delete;
    PhysicalPixelsScope& operator=(PhysicalPixelsScope&&) = delete;

private:
    DPI_AWARENESS_CONTEXT previous_;
};

std::string utf8(const wchar_t* text) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    result.pop_back(); // the NUL
    return result;
}

/// The monitor's device interface path: stable across reboots and display renumbering. The GDI
/// name ("\\.\DISPLAY1") stands in when there is none.
std::string stable_name(const wchar_t* gdi_name) {
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    if (EnumDisplayDevicesW(gdi_name, 0, &device, EDD_GET_DEVICE_INTERFACE_NAME) != FALSE &&
        device.DeviceID[0] != L'\0') {
        return utf8(std::data(device.DeviceID));
    }
    return utf8(gdi_name);
}

/// Monitors' names as Display settings shows them, by their GDI name ("\\.\DISPLAY1").
std::map<std::wstring, std::string> monitor_names() {
    UINT32 path_count = 0;
    UINT32 mode_count = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) !=
        ERROR_SUCCESS) {
        return {};
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count,
                           modes.data(), nullptr) != ERROR_SUCCESS) {
        return {};
    }
    paths.resize(path_count);
    std::map<std::wstring, std::string> names;
    for (const DISPLAYCONFIG_PATH_INFO& path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS &&
            DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS &&
            target.monitorFriendlyDeviceName[0] != L'\0') {
            names.emplace(std::data(source.viewGdiDeviceName),
                          utf8(std::data(target.monitorFriendlyDeviceName)));
        }
    }
    return names;
}

BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC /*dc*/, LPRECT /*rect*/, LPARAM context) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr): EnumDisplayMonitors passes our pointer back
    reinterpret_cast<std::vector<HMONITOR>*>(context)->push_back(monitor);
    return TRUE;
}

} // namespace

std::vector<win::PhysicalMonitor> physical_monitors() {
    const PhysicalPixelsScope scope;
    std::vector<HMONITOR> handles;
    EnumDisplayMonitors(nullptr, nullptr, collect_monitor, reinterpret_cast<LPARAM>(&handles));
    const std::map<std::wstring, std::string> names = monitor_names();
    std::vector<win::PhysicalMonitor> monitors;
    for (HMONITOR handle : handles) {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(handle, &info) == FALSE) {
            continue;
        }
        UINT dpi_x = USER_DEFAULT_SCREEN_DPI;
        UINT dpi_y = USER_DEFAULT_SCREEN_DPI;
        if (FAILED(GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)) || dpi_x == 0) {
            dpi_x = USER_DEFAULT_SCREEN_DPI;
        }
        monitors.push_back({.key = win::monitor_key(stable_name(std::data(info.szDevice))),
                            .left = info.rcMonitor.left,
                            .top = info.rcMonitor.top,
                            .right = info.rcMonitor.right,
                            .bottom = info.rcMonitor.bottom,
                            .dpi = dpi_x,
                            .primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0,
                            .work = {.left = info.rcWork.left,
                                     .top = info.rcWork.top,
                                     .right = info.rcWork.right,
                                     .bottom = info.rcWork.bottom},
                            .name = names.contains(std::data(info.szDevice))
                                        ? names.at(std::data(info.szDevice))
                                        : std::string()});
    }
    return monitors;
}

} // namespace osdrop::engine
