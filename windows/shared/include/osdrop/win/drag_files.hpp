#pragma once

// The portable half of the PC's drag bridge (DEVPLAN Phase 4). A drag Osdrop starts on the PC
// offers its files as virtual files: a FILEGROUPDESCRIPTORW names every file and folder of the
// dragged trees, and Explorer (or the app) reads each file through a stream when it wants it. A
// drag the PC captures arrives on thin drop targets along the edges a cursor can leave through.
// Everything here is free of Windows headers, so it is unit tested and linted on every host.

#include <osdrop/win/screens.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace osdrop::win::drag {

/// FILEDESCRIPTORW.cFileName holds MAX_PATH UTF-16 units with its NUL.
inline constexpr std::size_t kMaxDescriptorPath = 259;

/// One entry of the dragged trees as the source has them, parents before children.
struct TreeEntry {
    std::string path; ///< relative to the drag, '/' between names, UTF-8; top-level items have none
    bool folder = false;
    std::uint64_t size = 0;       ///< bytes; 0 for folders
    std::uint64_t write_time = 0; ///< FILETIME units (100 ns since 1601); 0 when not known
};

/// One FILEDESCRIPTORW: the path as Explorer creates it.
struct VirtualFile {
    std::u16string path; ///< '\' between names
    bool folder = false;
    std::uint64_t size = 0;
    std::uint64_t write_time = 0;
    std::size_t entry = 0; ///< its index in the entries given
    bool operator==(const VirtualFile&) const = default;
};

struct VirtualFiles {
    std::vector<VirtualFile> files;
    /// Entries left out: a path longer than a descriptor holds, an invalid one, or one below an
    /// entry left out.
    std::size_t skipped = 0;
};

/// The descriptors for `entries`. Names are taken as they are: local files already fit this PC,
/// and names from other computers are mapped by core before they get here.
[[nodiscard]] VirtualFiles virtual_files(std::span<const TreeEntry> entries);

/// UTF-8 as UTF-16; invalid sequences become U+FFFD.
[[nodiscard]] std::u16string utf16(std::string_view text);

/// The content of one file read front to back: a local file, or a transfer as it arrives. `read`
/// may wait for bytes; it returns 0 at the end, or when the content cannot be read further.
class ContentReader {
public:
    virtual ~ContentReader() = default;
    [[nodiscard]] virtual std::size_t read(std::span<std::uint8_t> buffer) = 0;
};

/// Where a drag's files come from; `open` starts a file over from its first byte, or returns null
/// when it cannot be read.
class ContentSource {
public:
    virtual ~ContentSource() = default;
    [[nodiscard]] virtual std::unique_ptr<ContentReader> open(std::size_t entry) = 0;
};

/// One virtual file as an IStream sees it. Reading goes front to back; seeking forward reads and
/// drops the bytes in between, seeking back starts the file over (Explorer only seeks to the start
/// or to where it is, but other apps are not bound to).
class FileStream {
public:
    FileStream(ContentSource& source, std::size_t entry, std::uint64_t size);

    /// Up to `buffer.size()` bytes; fewer only at the end or when the content broke off.
    [[nodiscard]] std::size_t read(std::span<std::uint8_t> buffer);
    /// Moves to `offset`; false past the end or when the content cannot be read again.
    [[nodiscard]] bool seek(std::uint64_t offset);
    [[nodiscard]] std::uint64_t position() const { return position_; }
    [[nodiscard]] std::uint64_t size() const { return size_; }

private:
    [[nodiscard]] bool reopen();

    ContentSource* source_;
    std::size_t entry_;
    std::uint64_t size_;
    std::uint64_t position_ = 0;
    std::unique_ptr<ContentReader> reader_;
};

/// Strips `thickness` pixels wide inside every outer edge of the monitors: the parts of their
/// edges that no other monitor continues, where a cursor can leave the PC.
[[nodiscard]] std::vector<PixelRect> edge_strips(std::span<const PhysicalMonitor> monitors,
                                                 std::int32_t thickness);

} // namespace osdrop::win::drag
