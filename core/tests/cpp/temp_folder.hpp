#pragma once

// A fresh folder under the system's temporary folder, removed with everything in it.

#include "transfer/files.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace osdrop::test {

class TempFolder {
public:
    TempFolder() {
        static std::atomic<std::uint32_t> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("osdrop-test-" + std::to_string(stamp) + "-" + std::to_string(++counter));
        std::filesystem::create_directories(path_);
    }
    ~TempFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(transfer::native_path(path_), ignored);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
    TempFolder(TempFolder&&) = delete;
    TempFolder& operator=(TempFolder&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    /// `relative` is UTF-8.
    [[nodiscard]] std::filesystem::path operator/(std::string_view relative) const {
        return path_ / transfer::path_from_utf8(relative);
    }

private:
    std::filesystem::path path_;
};

/// Creates `path` and its missing parents one level at a time, each in its native form, so paths
/// longer than 260 characters work on Windows. True when `path` is a folder afterwards.
inline bool create_folders(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::is_directory(transfer::native_path(path), error)) {
        return true;
    }
    if (path.has_parent_path() && path.parent_path() != path &&
        !create_folders(path.parent_path())) {
        return false;
    }
    std::filesystem::create_directory(transfer::native_path(path), error);
    return std::filesystem::is_directory(transfer::native_path(path), error);
}

inline void write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    create_folders(path.parent_path());
    std::ofstream out(transfer::native_path(path), std::ios::binary | std::ios::trunc);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): bytes as the stream's chars
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

inline void write_file(const std::filesystem::path& path, std::string_view text) {
    write_file(path, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), // NOLINT
                               text.size()));
}

inline std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(transfer::native_path(path), std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// Bytes 0, 1, 2... 255, 0, 1... with `seed` added.
inline std::vector<std::uint8_t> pattern(std::size_t size, std::uint8_t seed = 0) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::uint8_t>(i + seed);
    }
    return bytes;
}

/// Everything below `folder`, as UTF-8 paths relative to it, '/' separated.
inline std::vector<std::string> listing(const std::filesystem::path& folder) {
    std::vector<std::string> out;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             transfer::native_path(folder), std::filesystem::directory_options::none)) {
        out.push_back(transfer::utf8_of(
            std::filesystem::path(entry.path().lexically_relative(transfer::native_path(folder)))
                .generic_u8string()));
    }
    std::ranges::sort(out);
    return out;
}

} // namespace osdrop::test
