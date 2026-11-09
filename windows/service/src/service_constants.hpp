#pragma once

#include <cstdint>

namespace osdrop::service {

inline constexpr const wchar_t* kServiceName = L"OsdropService";
inline constexpr const wchar_t* kServiceDisplayName = L"Osdrop Input";
inline constexpr const wchar_t* kServiceDescription =
    L"Keeps Osdrop's input process running in the signed-in user's session, so this PC's mouse "
    L"and keyboard can control other computers and be controlled by them.";
inline constexpr std::uint32_t kServiceRestartDelayMilliseconds = 5000;
inline constexpr std::uint32_t kServiceStopTimeoutMilliseconds = 10000;

} // namespace osdrop::service
