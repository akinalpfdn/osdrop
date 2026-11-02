#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Names that the installer, the service, the input process and the app must agree on. Each one
// derives from the identifier prefix com.akinalpfdn.osdrop (DEVPLAN, cross-cutting decisions).
namespace osdrop::win {

inline constexpr std::wstring_view kServiceName = L"OsdropService";
inline constexpr std::wstring_view kServiceExecutable = L"OsdropService.exe";
inline constexpr std::wstring_view kInputExecutable = L"OsdropInput.exe";
inline constexpr std::wstring_view kAppExecutable = L"Osdrop.exe";

// The input process listens on one pipe per Windows session (DEVPLAN, Windows constraints).
inline constexpr std::wstring_view kInputPipePrefix = LR"(\\.\pipe\com.akinalpfdn.osdrop.input.)";

// Full pipe name for the input process of the given Windows session.
std::wstring input_pipe_name(std::uint32_t session_id);

} // namespace osdrop::win
