#include <osdrop/win/clipboard_text.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <map>
#include <utility>

namespace osdrop::win {
namespace {

constexpr char32_t kMaxCodePoint = 0x10FFFF;
constexpr char32_t kSurrogateFirst = 0xD800;
constexpr char32_t kSurrogateLast = 0xDFFF;
constexpr char16_t kHighFirst = 0xD800;
constexpr char16_t kLowFirst = 0xDC00;
constexpr char16_t kLowLast = 0xDFFF;
constexpr std::size_t kOffsetDigits = 10;

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
}

/// The next code point of UTF-8 `text` at `at`, and its length; nullopt for invalid UTF-8.
std::optional<std::pair<char32_t, std::size_t>> next_code_point(std::string_view text,
                                                                std::size_t at) {
    const auto lead = static_cast<unsigned char>(text[at]);
    std::size_t length = 0;
    char32_t cp = 0;
    char32_t minimum = 0;
    if (lead < 0x80U) {
        return std::pair(char32_t{lead}, std::size_t{1});
    }
    if ((lead & 0xE0U) == 0xC0U) {
        length = 2;
        cp = lead & 0x1FU;
        minimum = 0x80;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3;
        cp = lead & 0x0FU;
        minimum = 0x800;
    } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4;
        cp = lead & 0x07U;
        minimum = 0x10000;
    } else {
        return std::nullopt;
    }
    if (at + length > text.size()) {
        return std::nullopt;
    }
    for (std::size_t i = 1; i < length; ++i) {
        const auto next = static_cast<unsigned char>(text[at + i]);
        if ((next & 0xC0U) != 0x80U) {
            return std::nullopt;
        }
        cp = (cp << 6U) | (next & 0x3FU);
    }
    if (cp < minimum || cp > kMaxCodePoint || (cp >= kSurrogateFirst && cp <= kSurrogateLast)) {
        return std::nullopt;
    }
    return std::pair(cp, length);
}

std::string padded(std::size_t value) {
    const std::string digits = std::to_string(value);
    return std::string(kOffsetDigits - std::min(kOffsetDigits, digits.size()), '0') + digits;
}

/// The header's numeric fields, up to the first line that is not "Key:value".
std::map<std::string, long long> header_of(std::string_view block) {
    std::map<std::string, long long> fields;
    std::size_t at = 0;
    while (at < block.size() && block[at] != '<') {
        const std::size_t end = block.find_first_of("\r\n", at);
        const std::string_view line =
            block.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            break;
        }
        long long value = 0;
        const std::string_view number = line.substr(colon + 1);
        if (std::from_chars(number.data(), number.data() + number.size(), value).ec ==
            std::errc{}) {
            fields[std::string(line.substr(0, colon))] = value;
        }
        if (end == std::string_view::npos) {
            break;
        }
        at = block.find_first_not_of("\r\n", end);
    }
    return fields;
}

} // namespace

std::optional<std::string> utf8_of_utf16(std::u16string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size() && text[i] != u'\0'; ++i) {
        const char16_t unit = text[i];
        if (unit < kHighFirst || unit > kLowLast) {
            append_utf8(out, unit);
            continue;
        }
        if (unit >= kLowFirst || i + 1 >= text.size() || text[i + 1] < kLowFirst ||
            text[i + 1] > kLowLast) {
            return std::nullopt; // an unpaired surrogate
        }
        const char32_t high = unit - kHighFirst;
        const char32_t low = text[i + 1] - kLowFirst;
        append_utf8(out, 0x10000 + ((high << 10U) | low));
        ++i;
    }
    return out;
}

std::optional<std::u16string> utf16_of_utf8(std::string_view text) {
    std::u16string out;
    out.reserve(text.size());
    for (std::size_t at = 0; at < text.size();) {
        const auto next = next_code_point(text, at);
        if (!next) {
            return std::nullopt;
        }
        const auto [cp, length] = *next;
        if (cp < 0x10000) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            const char32_t value = cp - 0x10000;
            out.push_back(static_cast<char16_t>(kHighFirst + (value >> 10U)));
            out.push_back(static_cast<char16_t>(kLowFirst + (value & 0x3FFU)));
        }
        at += length;
    }
    return out;
}

std::optional<std::string> html_of_cf_html(std::span<const std::uint8_t> block) {
    const std::string_view text(reinterpret_cast<const char*>(block.data()), // NOLINT: bytes
                                block.size());
    const auto fields = header_of(text);
    const auto field = [&](const char* key) -> long long {
        const auto it = fields.find(key);
        return it == fields.end() ? -1 : it->second;
    };
    long long start = field("StartHTML");
    long long end = field("EndHTML");
    if (start < 0 || end < 0) { // some producers give only the fragment
        start = field("StartFragment");
        end = field("EndFragment");
    }
    if (start < 0 || end < start || std::cmp_greater(end, text.size())) {
        return std::nullopt;
    }
    return std::string(
        text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start)));
}

std::vector<std::uint8_t> cf_html_of_html(std::string_view html) {
    static constexpr std::string_view kBefore = "<html><body>\r\n<!--StartFragment-->";
    static constexpr std::string_view kAfter = "<!--EndFragment-->\r\n</body></html>";
    const auto header = [](std::size_t start_html, std::size_t end_html, std::size_t start_fragment,
                           std::size_t end_fragment) {
        return "Version:0.9\r\nStartHTML:" + padded(start_html) +
               "\r\nEndHTML:" + padded(end_html) + "\r\nStartFragment:" + padded(start_fragment) +
               "\r\nEndFragment:" + padded(end_fragment) + "\r\n";
    };
    const std::size_t start_html = header(0, 0, 0, 0).size(); // the header has a fixed width
    const std::size_t start_fragment = start_html + kBefore.size();
    const std::size_t end_fragment = start_fragment + html.size();
    const std::size_t end_html = end_fragment + kAfter.size();
    const std::string block = header(start_html, end_html, start_fragment, end_fragment) +
                              std::string(kBefore) + std::string(html) + std::string(kAfter);
    return {block.begin(), block.end()};
}

} // namespace osdrop::win
