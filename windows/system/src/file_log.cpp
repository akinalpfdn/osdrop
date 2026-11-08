#include <osdrop/sys/file_log.hpp>

#include <windows.h>

#include <cstdint>
#include <format>
#include <fstream>
#include <string>
#include <system_error>

namespace osdrop::sys {
namespace {

constexpr std::uintmax_t kMaxBytes = std::uintmax_t{1024} * 1024;

std::filesystem::path logs_folder() {
    std::wstring program_data(MAX_PATH, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"ProgramData", program_data.data(),
                                                 static_cast<DWORD>(program_data.size()));
    if (length == 0 || length >= program_data.size()) {
        return {};
    }
    program_data.resize(length);
    return std::filesystem::path(program_data) / L"Osdrop" / L"logs";
}

std::string timestamp() {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", now.wYear, now.wMonth,
                       now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
}

} // namespace

FileLog::FileLog(std::wstring_view name) {
    const std::filesystem::path folder = logs_folder();
    if (folder.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    path_ = folder / (std::wstring(name) + L".log");
}

void FileLog::write(std::string_view level, std::string_view message) {
    if (path_.empty()) {
        return;
    }
    const std::scoped_lock lock(mutex_);
    std::error_code error;
    if (std::filesystem::file_size(path_, error) > kMaxBytes && !error) {
        std::filesystem::path older = path_;
        older += L".1";
        std::filesystem::rename(path_, older, error);
    }
    std::ofstream file(path_, std::ios::app | std::ios::binary);
    file << timestamp() << ' ' << level << ' ' << message << "\r\n";
}

} // namespace osdrop::sys
