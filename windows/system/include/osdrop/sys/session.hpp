#pragma once

// Sessions, their users and the names derived from them, for the input process, the service and
// the pipe's clients.

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace osdrop::sys {

/// The session this process runs in.
[[nodiscard]] DWORD current_session();
/// The string SID of the user logged on in `session`; needs LocalSystem (WTSQueryUserToken).
[[nodiscard]] std::optional<std::wstring> session_user_sid(DWORD session);
/// The string SID of a token's user.
[[nodiscard]] std::optional<std::wstring> token_user_sid(HANDLE token);

/// \\.\pipe\com.akinalpfdn.osdrop.input.<session>
[[nodiscard]] std::wstring input_pipe_name(DWORD session);
/// The event the service sets to ask OsdropInput in `session` to quit (Global\...).
[[nodiscard]] std::wstring input_stop_event_name(DWORD session);

/// The folder of this process's executable.
[[nodiscard]] std::optional<std::wstring> executable_folder();
/// The full path of another process's executable.
[[nodiscard]] std::optional<std::wstring> process_image(DWORD process_id);
/// Paths compared as Windows does: without regard to case.
[[nodiscard]] bool same_path(std::wstring_view a, std::wstring_view b);

/// SIDs and paths for log lines: UTF-8, control characters as '?', so a line stays one line.
[[nodiscard]] std::string log_text(std::wstring_view text);

} // namespace osdrop::sys
