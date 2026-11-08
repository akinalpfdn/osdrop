#pragma once

// File access for transfers over std::filesystem: UTF-8 paths, long paths on Windows, times in
// microseconds since 1970, free space, ranged reads, and a writer that never leaves a partial file
// behind: it writes a hidden temporary next to the target and moves it into place only when size
// and digest match, never over an existing file.

#include "crypto/primitives.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace osdrop::transfer {

/// A path from UTF-8 text (std::filesystem would read a narrow string in the ANSI code page on
/// Windows).
[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view text);
[[nodiscard]] std::string utf8_of(const std::filesystem::path& path);

/// The path to give the operating system: on Windows, absolute paths get the \\?\ prefix, so
/// paths longer than 260 characters work; elsewhere the path itself.
[[nodiscard]] std::filesystem::path native_path(const std::filesystem::path& path);

/// The modification time of the file or folder at `path`, following links.
[[nodiscard]] std::optional<std::int64_t> modified_us(const std::filesystem::path& path);
/// The modification time of the link itself.
[[nodiscard]] std::optional<std::int64_t> link_modified_us(const std::filesystem::path& path);
/// Best effort: a time that cannot be set leaves the file as it is.
void set_modified_us(const std::filesystem::path& path, std::int64_t microseconds);

/// Whether the owner may execute the file (always false on Windows).
[[nodiscard]] bool is_executable(const std::filesystem::path& path);

/// Bytes a new file may take on the volume of `path`.
[[nodiscard]] std::optional<std::uint64_t> free_space(const std::filesystem::path& path);

/// Creates a folder named `name` in `parent`, numbered " (2)", " (3)"... when the name is taken;
/// the folder created.
[[nodiscard]] Expected<std::filesystem::path, Error>
create_unique_folder(const std::filesystem::path& parent, std::string_view name);

/// Creates a symbolic link to `target` (UTF-8, '/' separated, relative). NOT_PERMITTED when the
/// system does not let this user create links (Windows without Developer Mode).
[[nodiscard]] Status<Error> create_link(const std::filesystem::path& link, std::string_view target,
                                        bool to_folder);

class FileReader {
public:
    [[nodiscard]] static Expected<FileReader, Error> open(const std::filesystem::path& path);

    /// Up to out.size() bytes from `offset`; fewer at the end of the file, 0 past it.
    [[nodiscard]] Expected<std::size_t, Error> read(std::uint64_t offset,
                                                    std::span<std::uint8_t> out);

private:
    explicit FileReader(std::ifstream stream) : stream_(std::move(stream)) {}
    std::ifstream stream_;
    std::uint64_t position_ = 0;
};

/// Writes one received file: bytes in order, hashed as they come, into a hidden temporary in the
/// target's folder. Destroying an unfinished writer removes the temporary.
class FileWriter {
public:
    /// `token` makes the temporary's name unique among writers (transfer and entry).
    [[nodiscard]] static Expected<FileWriter, Error> create(const std::filesystem::path& target,
                                                            std::string_view token);
    ~FileWriter();
    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;
    FileWriter(FileWriter&& other) noexcept;
    FileWriter& operator=(FileWriter&&) = delete;

    /// NO_SPACE when the volume is full, IO_ERROR for other failures.
    [[nodiscard]] Status<Error> write(std::span<const std::uint8_t> data);

    /// Checks the size and digest (INTEGRITY_FAILED, temporary removed), then moves the file to
    /// the target, numbered when that name is taken; the final path.
    [[nodiscard]] Expected<std::filesystem::path, Error>
    finish(std::uint64_t expected_size, const crypto::ContentHasher::Digest& expected_digest,
           std::int64_t modified_us, bool executable);

    /// Removes the temporary.
    void abort();

private:
    FileWriter(std::filesystem::path target, std::filesystem::path temporary,
               std::unique_ptr<std::ofstream> stream)
        : target_(std::move(target)), temporary_(std::move(temporary)), stream_(std::move(stream)),
          open_(true) {}

    std::filesystem::path target_;
    std::filesystem::path temporary_;
    std::unique_ptr<std::ofstream>
        stream_; ///< a pointer, so moving never throws (MSVC streams can)
    crypto::ContentHasher hasher_;
    std::uint64_t written_ = 0;
    bool open_ = false;
};

} // namespace osdrop::transfer
