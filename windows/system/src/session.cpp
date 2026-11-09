#include <osdrop/sys/handles.hpp>
#include <osdrop/sys/session.hpp>
#include <osdrop/win/input_protocol.hpp>

#include <sddl.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

namespace osdrop::sys {
namespace {

constexpr std::size_t kMaxPath = 32768; // the longest path Windows has

} // namespace

DWORD current_session() {
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
        return 0xFFFFFFFF;
    }
    return session;
}

std::optional<std::wstring> token_user_sid(HANDLE token) {
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0) {
        return std::nullopt;
    }
    std::vector<std::byte> buffer(size);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        return std::nullopt;
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &text)) {
        return std::nullopt;
    }
    const std::unique_ptr<void, LocalFreer> owned(text);
    return std::wstring(text);
}

std::optional<std::wstring> session_user_sid(DWORD session) {
    HANDLE raw = nullptr;
    if (!WTSQueryUserToken(session, &raw)) {
        return std::nullopt; // nobody is logged on there
    }
    const UniqueHandle token(raw);
    return token_user_sid(token.get());
}

std::wstring input_pipe_name(DWORD session) {
    return std::wstring(win::input::kPipePrefix) + std::to_wstring(session);
}

std::wstring input_stop_event_name(DWORD session) {
    return L"Global\\com.akinalpfdn.osdrop.input.stop." + std::to_wstring(session);
}

std::optional<std::wstring> executable_folder() {
    std::wstring folder(kMaxPath, L'\0');
    const DWORD length =
        GetModuleFileNameW(nullptr, folder.data(), static_cast<DWORD>(folder.size()));
    if (length == 0 || length == folder.size()) {
        return std::nullopt;
    }
    folder.resize(length);
    const std::size_t slash = folder.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return std::nullopt;
    }
    folder.resize(slash);
    return folder;
}

std::optional<std::wstring> process_image(DWORD process_id) {
    const UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
    if (!process) {
        return std::nullopt;
    }
    std::wstring path(kMaxPath, L'\0');
    auto length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &length)) {
        return std::nullopt;
    }
    path.resize(length);
    return path;
}

bool same_path(std::wstring_view a, std::wstring_view b) {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

std::string log_text(std::wstring_view text) {
    std::wstring printable(text);
    std::ranges::replace_if(printable, [](wchar_t c) { return c < L' ' || c == 0x7F; }, L'?');
    const int bytes =
        WideCharToMultiByte(CP_UTF8, 0, printable.data(), static_cast<int>(printable.size()),
                            nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, printable.data(), static_cast<int>(printable.size()),
                        result.data(), bytes, nullptr, nullptr);
    return result;
}

} // namespace osdrop::sys
