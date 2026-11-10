#include <osdrop/win/drag_files.hpp>

#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace osdrop::win::drag {
namespace {

constexpr char16_t kReplacement = 0xFFFD;
constexpr std::size_t kSkipChunk = std::size_t{64} * 1024;

/// Extensions (lowercase) and the types macOS gives them.
constexpr std::array<std::pair<std::string_view, std::string_view>, 37> kContentTypes{{
    {"aac", "public.aac-audio"},
    {"avi", "public.avi"},
    {"bmp", "com.microsoft.bmp"},
    {"csv", "public.comma-separated-values-text"},
    {"doc", "com.microsoft.word.doc"},
    {"docx", "org.openxmlformats.wordprocessingml.document"},
    {"exe", "com.microsoft.windows-executable"},
    {"flac", "org.xiph.flac"},
    {"gif", "com.compuserve.gif"},
    {"heic", "public.heic"},
    {"heif", "public.heif"},
    {"htm", "public.html"},
    {"html", "public.html"},
    {"jpeg", "public.jpeg"},
    {"jpg", "public.jpeg"},
    {"json", "public.json"},
    {"m4a", "com.apple.m4a-audio"},
    {"m4v", "com.apple.m4v-video"},
    {"mkv", "org.matroska.mkv"},
    {"mov", "com.apple.quicktime-movie"},
    {"mp3", "public.mp3"},
    {"mp4", "public.mpeg-4"},
    {"pdf", "com.adobe.pdf"},
    {"png", "public.png"},
    {"ppt", "com.microsoft.powerpoint.ppt"},
    {"pptx", "org.openxmlformats.presentationml.presentation"},
    {"rtf", "public.rtf"},
    {"svg", "public.svg-image"},
    {"tif", "public.tiff"},
    {"tiff", "public.tiff"},
    {"txt", "public.plain-text"},
    {"wav", "com.microsoft.waveform-audio"},
    {"webp", "org.webmproject.webp"},
    {"xls", "com.microsoft.excel.xls"},
    {"xlsx", "org.openxmlformats.spreadsheetml.sheet"},
    {"xml", "public.xml"},
    {"zip", "public.zip-archive"},
}};

/// A relative path of names joined by '/', none of them empty, "." or "..".
bool valid_path(std::string_view path) {
    if (path.empty()) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view name = path.substr(start, end - start);
        if (name.empty() || name == "." || name == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

std::string_view parent_of(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view() : path.substr(0, slash);
}

/// The parts of [begin, end) that `covered` does not cover.
std::vector<std::pair<std::int32_t, std::int32_t>>
uncovered(std::int32_t begin, std::int32_t end,
          std::vector<std::pair<std::int32_t, std::int32_t>> covered) {
    std::ranges::sort(covered);
    std::vector<std::pair<std::int32_t, std::int32_t>> parts;
    std::int32_t from = begin;
    for (const auto& [low, high] : covered) {
        if (high <= from || low >= end) {
            continue;
        }
        if (low > from) {
            parts.emplace_back(from, low);
        }
        from = std::max(from, high);
    }
    if (from < end) {
        parts.emplace_back(from, end);
    }
    return parts;
}

} // namespace

std::u16string utf16(std::string_view text) {
    std::u16string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        char32_t code = 0;
        if (lead < 0x80U) {
            length = 1;
            code = lead;
        } else if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
            code = lead & 0x1FU;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
            code = lead & 0x0FU;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
            code = lead & 0x07U;
        }
        bool valid = length != 0 && i + length <= text.size();
        for (std::size_t k = 1; valid && k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            valid = (next & 0xC0U) == 0x80U;
            code = (code << 6U) | (next & 0x3FU);
        }
        constexpr std::array<char32_t, 5> kSmallest{0, 0, 0x80, 0x800, 0x10000};
        valid = valid && code >= kSmallest.at(length) && code <= 0x10FFFF &&
                (code < 0xD800 || code > 0xDFFF);
        if (!valid) {
            out.push_back(kReplacement);
            ++i;
            continue;
        }
        if (code >= 0x10000) {
            const char32_t value = code - 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (value >> 10U)));
            out.push_back(static_cast<char16_t>(0xDC00 + (value & 0x3FFU)));
        } else {
            out.push_back(static_cast<char16_t>(code));
        }
        i += length;
    }
    return out;
}

VirtualFiles virtual_files(std::span<const TreeEntry> entries) {
    VirtualFiles result;
    std::set<std::string, std::less<>> folders; // described folders, by their source path
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const TreeEntry& entry = entries[index];
        const std::string_view parent = parent_of(entry.path);
        std::u16string path = utf16(entry.path);
        std::ranges::replace(path, u'/', u'\\');
        if (!valid_path(entry.path) || (!parent.empty() && !folders.contains(parent)) ||
            path.size() > kMaxDescriptorPath) {
            ++result.skipped; // its children are left out with it: their parent is not described
            continue;
        }
        if (entry.folder) {
            folders.insert(entry.path);
        }
        result.files.push_back({.path = std::move(path),
                                .folder = entry.folder,
                                .size = entry.folder ? 0 : entry.size,
                                .write_time = entry.write_time,
                                .entry = index});
    }
    return result;
}

std::string content_type(std::string_view name, bool folder) {
    if (folder) {
        return "public.folder";
    }
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) {
        return "public.data";
    }
    std::string extension(name.substr(dot + 1));
    std::ranges::transform(extension, extension.begin(), [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    });
    const auto* found = std::ranges::find(kContentTypes, std::string_view(extension),
                                          &std::pair<std::string_view, std::string_view>::first);
    return std::string(found != kContentTypes.end() ? found->second : "public.data");
}

FileStream::FileStream(ContentSource& source, std::size_t entry, std::uint64_t size)
    : source_(&source), entry_(entry), size_(size) {}

bool FileStream::reopen() {
    position_ = 0;
    broken_ = false;
    reader_ = source_->open(entry_);
    return reader_ != nullptr;
}

std::size_t FileStream::read(std::span<std::uint8_t> buffer) {
    if (!reader_ && (position_ != 0 || !reopen())) {
        broken_ = position_ < size_;
        return 0;
    }
    const auto wanted =
        static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), size_ - position_));
    std::size_t done = 0;
    while (done < wanted) {
        const std::size_t got = reader_->read(buffer.subspan(done, wanted - done));
        if (got == 0) {
            broken_ = true; // the content ended early or broke off
            break;
        }
        done += got;
    }
    position_ += done;
    return done;
}

bool FileStream::seek(std::uint64_t offset) {
    if (offset > size_) {
        return false;
    }
    if ((offset < position_ || !reader_) && !reopen()) {
        return false;
    }
    std::vector<std::uint8_t> skip(
        static_cast<std::size_t>(std::min<std::uint64_t>(kSkipChunk, offset - position_)));
    while (position_ < offset) {
        const auto chunk =
            static_cast<std::size_t>(std::min<std::uint64_t>(skip.size(), offset - position_));
        if (read(std::span(skip).first(chunk)) == 0) {
            return false;
        }
    }
    return true;
}

namespace {

using Spans = std::vector<std::pair<std::int32_t, std::int32_t>>;

/// What other monitors cover of each edge of `m`: left, right, top, bottom.
std::array<Spans, 4> covered_edges(const PhysicalMonitor& m,
                                   std::span<const PhysicalMonitor> monitors) {
    std::array<Spans, 4> covered;
    for (const PhysicalMonitor& o : monitors) {
        if (&o == &m) {
            continue;
        }
        if (o.right == m.left) {
            covered[0].emplace_back(o.top, o.bottom);
        }
        if (o.left == m.right) {
            covered[1].emplace_back(o.top, o.bottom);
        }
        if (o.bottom == m.top) {
            covered[2].emplace_back(o.left, o.right);
        }
        if (o.top == m.bottom) {
            covered[3].emplace_back(o.left, o.right);
        }
    }
    return covered;
}

} // namespace

std::vector<PixelRect> edge_strips(std::span<const PhysicalMonitor> monitors,
                                   std::int32_t thickness) {
    std::vector<PixelRect> strips;
    for (const PhysicalMonitor& m : monitors) {
        if (m.right <= m.left || m.bottom <= m.top) {
            continue;
        }
        auto [left, right, top, bottom] = covered_edges(m, monitors);
        for (const auto& [from, to] : uncovered(m.top, m.bottom, std::move(left))) {
            strips.push_back(
                {.left = m.left, .top = from, .right = m.left + thickness, .bottom = to});
        }
        for (const auto& [from, to] : uncovered(m.top, m.bottom, std::move(right))) {
            strips.push_back(
                {.left = m.right - thickness, .top = from, .right = m.right, .bottom = to});
        }
        for (const auto& [from, to] : uncovered(m.left, m.right, std::move(top))) {
            strips.push_back(
                {.left = from, .top = m.top, .right = to, .bottom = m.top + thickness});
        }
        for (const auto& [from, to] : uncovered(m.left, m.right, std::move(bottom))) {
            strips.push_back(
                {.left = from, .top = m.bottom - thickness, .right = to, .bottom = m.bottom});
        }
    }
    return strips;
}

} // namespace osdrop::win::drag
