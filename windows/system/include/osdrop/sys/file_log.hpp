#pragma once

#include <osdrop/win/input_session.hpp>

#include <filesystem>
#include <mutex>
#include <string_view>

namespace osdrop::sys {

/// Appends timestamped lines to %ProgramData%\Osdrop\logs\<name>.log and keeps one older file.
/// Never holds keystrokes or file contents, only what happened.
class FileLog final : public win::input::InputLog {
public:
    explicit FileLog(std::wstring_view name);

    void info(std::string_view message) override { write("INFO", message); }
    void warning(std::string_view message) override { write("WARN", message); }
    void error(std::string_view message) { write("ERROR", message); }

private:
    void write(std::string_view level, std::string_view message);

    std::filesystem::path path_;
    std::mutex mutex_;
};

} // namespace osdrop::sys
