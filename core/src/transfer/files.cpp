#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "transfer/files.hpp"
#include "transfer/names.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <exception>
#include <ios>
#include <string>
#include <system_error>
#include <utility>

namespace osdrop::transfer {
namespace {

namespace fs = std::filesystem;
using std::chrono::microseconds;

constexpr int kMaxNumber = 10'000; ///< "name (10000)": give up beyond, the folder is full of them

std::int64_t to_us(fs::file_time_type time) {
#ifdef _MSC_VER
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
#else
    const auto system = fs::file_time_type::clock::to_sys(time);
#endif
    return std::chrono::duration_cast<microseconds>(system.time_since_epoch()).count();
}

fs::file_time_type from_us(std::int64_t value) {
    const std::chrono::sys_time<microseconds> system{microseconds{value}};
#ifdef _MSC_VER
    return std::chrono::clock_cast<fs::file_time_type::clock>(system);
#else
    return std::chrono::time_point_cast<fs::file_time_type::duration>(
        fs::file_time_type::clock::from_sys(system));
#endif
}

enum class Moved : std::uint8_t { Done, Taken, Failed };

/// Moves `from` to `to` unless something exists at `to`.
Moved move_new(const fs::path& from, const fs::path& to) {
#ifdef _WIN32
    if (MoveFileExW(native_path(from).c_str(), native_path(to).c_str(), 0) != 0) {
        return Moved::Done;
    }
    const DWORD error = GetLastError();
    return error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ? Moved::Taken
                                                                       : Moved::Failed;
#else
    // A hard link fails when the name exists, so it claims the name atomically.
    if (::link(from.c_str(), to.c_str()) == 0) {
        std::error_code ignored;
        fs::remove(from, ignored);
        return Moved::Done;
    }
    if (errno == EEXIST) {
        return Moved::Taken;
    }
    // File systems without hard links (FAT, exFAT, some network shares): check, then rename.
    std::error_code error;
    if (fs::exists(fs::symlink_status(to, error))) {
        return Moved::Taken;
    }
    fs::rename(from, to, error);
    return error ? Moved::Failed : Moved::Done;
#endif
}

Error io_error_for(const std::error_code& error) {
    if (error == std::errc::no_space_on_device) {
        return Error::NoSpace;
    }
    if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted) {
        return Error::NotPermitted;
    }
    return Error::IoError;
}

} // namespace

fs::path path_from_utf8(std::string_view text) {
    return {std::u8string(text.begin(), text.end())};
}

std::string utf8_of(const fs::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

fs::path native_path(const fs::path& path) {
#ifdef _WIN32
    if (!path.is_absolute()) {
        return path;
    }
    const std::wstring text = fs::path(path).make_preferred().native();
    if (text.starts_with(LR"(\\?\)")) {
        return path;
    }
    if (text.starts_with(LR"(\\)")) {
        return {LR"(\\?\UNC\)" + text.substr(2)};
    }
    return {LR"(\\?\)" + text};
#else
    return path;
#endif
}

std::optional<std::int64_t> modified_us(const fs::path& path) {
    std::error_code error;
    const fs::file_time_type time = fs::last_write_time(native_path(path), error);
    if (error) {
        return std::nullopt;
    }
    return to_us(time);
}

std::optional<std::int64_t> link_modified_us(const fs::path& path) {
#ifdef _WIN32
    // Attributes of a reparse point itself: GetFileAttributesEx does not follow links.
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(native_path(path).c_str(), GetFileExInfoStandard, &data) == 0) {
        return std::nullopt;
    }
    constexpr std::int64_t kTicksTo1970 = 116'444'736'000'000'000; // 100 ns ticks from 1601
    const auto ticks = static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32U) |
        data.ftLastWriteTime.dwLowDateTime);
    return (ticks - kTicksTo1970) / 10;
#else
    struct stat info{};
    if (::lstat(path.c_str(), &info) != 0) {
        return std::nullopt;
    }
#ifdef __APPLE__
    const timespec& time = info.st_mtimespec;
#else
    const timespec& time = info.st_mtim;
#endif
    return (static_cast<std::int64_t>(time.tv_sec) * 1'000'000) + (time.tv_nsec / 1'000);
#endif
}

void set_modified_us(const fs::path& path, std::int64_t microseconds) {
    std::error_code ignored;
    fs::last_write_time(native_path(path), from_us(microseconds), ignored);
}

bool is_executable(const fs::path& path) {
#ifdef _WIN32
    static_cast<void>(path);
    return false;
#else
    std::error_code error;
    const fs::file_status status = fs::status(path, error);
    return !error && (status.permissions() & fs::perms::owner_exec) != fs::perms::none;
#endif
}

std::optional<std::uint64_t> free_space(const fs::path& path) {
    std::error_code error;
    const fs::space_info space = fs::space(native_path(path), error);
    if (error) {
        return std::nullopt;
    }
    return space.available;
}

Expected<fs::path, Error> create_unique_folder(const fs::path& parent, std::string_view name) {
    for (int number = 1; number <= kMaxNumber; ++number) {
        fs::path candidate =
            parent / path_from_utf8(numbered_name(name, number, true, current_platform()));
        std::error_code error;
        if (fs::create_directory(native_path(candidate), error)) {
            return candidate;
        }
        if (error && error != std::errc::file_exists) {
            return Expected<fs::path, Error>::failure(io_error_for(error));
        }
    }
    return Expected<fs::path, Error>::failure(Error::IoError);
}

Status<Error> create_link(const fs::path& link, std::string_view target, bool to_folder) {
    fs::path target_path = path_from_utf8(target);
    target_path.make_preferred();
    std::error_code error;
    if (to_folder) {
        fs::create_directory_symlink(target_path, native_path(link), error);
    } else {
        fs::create_symlink(target_path, native_path(link), error);
    }
    if (!error) {
        return Status<Error>::ok();
    }
#ifdef _WIN32
    if (error.value() == ERROR_PRIVILEGE_NOT_HELD) {
        return Status<Error>::failure(Error::NotPermitted);
    }
#endif
    return Status<Error>::failure(io_error_for(error));
}

Expected<FileReader, Error> FileReader::open(const fs::path& path) {
    std::ifstream stream(native_path(path), std::ios::binary);
    if (!stream) {
        std::error_code error;
        return Expected<FileReader, Error>::failure(
            fs::exists(native_path(path), error) ? Error::IoError : Error::NotFound);
    }
    return FileReader(std::move(stream));
}

Expected<std::size_t, Error> FileReader::read(std::uint64_t offset, std::span<std::uint8_t> out) {
    if (offset != position_) {
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(offset));
        if (!stream_) {
            return Expected<std::size_t, Error>::failure(Error::IoError);
        }
        position_ = offset;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): bytes as the stream's chars
    stream_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    const auto count = static_cast<std::size_t>(stream_.gcount());
    if (stream_.bad()) {
        return Expected<std::size_t, Error>::failure(Error::IoError);
    }
    if (stream_.eof()) {
        stream_.clear(); // a later read at another offset must work
    }
    position_ += count;
    return count;
}

Expected<FileWriter, Error> FileWriter::create(const fs::path& target, std::string_view token) {
    fs::path temporary =
        target.parent_path() / path_from_utf8(".osdrop-" + std::string(token) + ".part");
    std::ofstream stream(native_path(temporary), std::ios::binary | std::ios::trunc);
    if (!stream) {
        std::error_code error;
        const auto space = fs::space(native_path(target.parent_path()), error);
        return Expected<FileWriter, Error>::failure(
            !error && space.available == 0 ? Error::NoSpace : Error::IoError);
    }
    return FileWriter(target, std::move(temporary), std::move(stream));
}

FileWriter::FileWriter(FileWriter&& other) noexcept
    : target_(std::move(other.target_)), temporary_(std::move(other.temporary_)),
      stream_(std::move(other.stream_)), hasher_(other.hasher_), written_(other.written_),
      open_(std::exchange(other.open_, false)) {}

FileWriter::~FileWriter() {
    try {
        abort();
    } catch (...) {
        std::terminate(); // only copying a path can throw (out of memory)
    }
}

Status<Error> FileWriter::write(std::span<const std::uint8_t> data) {
    if (!open_) {
        return Status<Error>::failure(Error::InvalidState);
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): bytes as the stream's chars
    stream_.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    if (!stream_) {
        const auto space = free_space(target_.parent_path());
        return Status<Error>::failure(space && *space < data.size() ? Error::NoSpace
                                                                    : Error::IoError);
    }
    hasher_.update(data);
    written_ += data.size();
    return Status<Error>::ok();
}

Expected<fs::path, Error> FileWriter::finish(std::uint64_t expected_size,
                                             const crypto::ContentHasher::Digest& expected_digest,
                                             std::int64_t modified_us, bool executable) {
    using Result = Expected<fs::path, Error>;
    if (!open_) {
        return Result::failure(Error::InvalidState);
    }
    stream_.close();
    if (!stream_) {
        abort();
        return Result::failure(Error::IoError);
    }
    if (written_ != expected_size || hasher_.finish() != expected_digest) {
        abort();
        return Result::failure(Error::IntegrityFailed);
    }
#ifndef _WIN32
    if (executable) {
        // Execute wherever read is allowed, as `chmod +x` would under the user's umask.
        std::error_code ignored;
        const fs::perms current = fs::status(temporary_, ignored).permissions();
        fs::perms add = fs::perms::none;
        add |= (current & fs::perms::owner_read) != fs::perms::none ? fs::perms::owner_exec
                                                                    : fs::perms::none;
        add |= (current & fs::perms::group_read) != fs::perms::none ? fs::perms::group_exec
                                                                    : fs::perms::none;
        add |= (current & fs::perms::others_read) != fs::perms::none ? fs::perms::others_exec
                                                                     : fs::perms::none;
        fs::permissions(temporary_, add, fs::perm_options::add, ignored);
    }
#else
    static_cast<void>(executable);
#endif
    set_modified_us(temporary_, modified_us);
    const std::string name = utf8_of(target_.filename());
    for (int number = 1; number <= kMaxNumber; ++number) {
        fs::path candidate = target_.parent_path() /
                             path_from_utf8(numbered_name(name, number, false, current_platform()));
        switch (move_new(temporary_, candidate)) {
        case Moved::Done:
            open_ = false;
            return candidate;
        case Moved::Taken:
            continue;
        case Moved::Failed:
            abort();
            return Result::failure(Error::IoError);
        }
    }
    abort();
    return Result::failure(Error::IoError);
}

void FileWriter::abort() {
    if (!open_) {
        return;
    }
    open_ = false;
    stream_.close();
    std::error_code ignored;
    fs::remove(native_path(temporary_), ignored);
}

} // namespace osdrop::transfer
