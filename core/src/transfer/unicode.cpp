#include "transfer/unicode.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>

namespace osdrop::transfer {
namespace {

struct Decomposition {
    char32_t code_point;
    std::uint16_t first;
    std::uint8_t length;
};

struct CombiningClass {
    char32_t code_point;
    std::uint8_t ccc;
};

struct Composition {
    char32_t first;
    char32_t second;
    char32_t composite;
};

struct CaseMapping {
    char32_t code_point;
    char32_t lower;
};

#include "transfer/unicode_tables.inc"

// Hangul syllables decompose and compose algorithmically (The Unicode Standard, 3.12).
constexpr char32_t kHangulBase = 0xAC00;
constexpr char32_t kLeadBase = 0x1100;
constexpr char32_t kVowelBase = 0x1161;
constexpr char32_t kTrailBase = 0x11A7;
constexpr char32_t kLeadCount = 19;
constexpr char32_t kVowelCount = 21;
constexpr char32_t kTrailCount = 28;
constexpr char32_t kSyllableCount = kLeadCount * kVowelCount * kTrailCount;

/// Every string of code points below U+0300 is already in NFC.
constexpr char32_t kFirstUnstable = 0x300;

template <typename Entry, std::size_t Size, typename Key>
const Entry* find(const Entry (&table)[Size], Key key) { // NOLINT(*-avoid-c-arrays): generated
    const std::span<const Entry> entries(table);
    const auto it = std::ranges::lower_bound(entries, key, {}, &Entry::code_point);
    return it != entries.end() && it->code_point == key ? &*it : nullptr;
}

std::uint8_t combining_class(char32_t cp) {
    const CombiningClass* entry = find(kCombiningClasses, cp);
    return entry == nullptr ? 0 : entry->ccc;
}

/// The full canonical decomposition of one code point, appended to `out`.
void decompose(char32_t cp, std::u32string& out) {
    std::u32string pending{cp}; // a stack: the next code point to decompose is at the back
    while (!pending.empty()) {
        const char32_t next = pending.back();
        pending.pop_back();
        if (next >= kHangulBase && next < kHangulBase + kSyllableCount) {
            const char32_t index = next - kHangulBase;
            out.push_back(kLeadBase + (index / (kVowelCount * kTrailCount)));
            out.push_back(kVowelBase + ((index % (kVowelCount * kTrailCount)) / kTrailCount));
            if (const char32_t trail = index % kTrailCount; trail != 0) {
                out.push_back(kTrailBase + trail);
            }
            continue;
        }
        const Decomposition* entry = find(kDecompositions, next);
        if (entry == nullptr) {
            out.push_back(next);
            continue;
        }
        const auto parts =
            std::span<const char32_t>(kDecomposed).subspan(entry->first, entry->length);
        pending.append(parts.rbegin(), parts.rend());
    }
}

/// Puts each run of non-starters in canonical order (stable by combining class).
void reorder(std::u32string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        if (combining_class(text[i]) == 0) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < text.size() && combining_class(text[end]) != 0) {
            ++end;
        }
        std::stable_sort(
            text.begin() + static_cast<std::ptrdiff_t>(i),
            text.begin() + static_cast<std::ptrdiff_t>(end),
            [](char32_t a, char32_t b) { return combining_class(a) < combining_class(b); });
        i = end;
    }
}

std::optional<char32_t> compose_pair(char32_t first, char32_t second) {
    if (first >= kLeadBase && first < kLeadBase + kLeadCount && second >= kVowelBase &&
        second < kVowelBase + kVowelCount) {
        return kHangulBase +
               ((((first - kLeadBase) * kVowelCount) + (second - kVowelBase)) * kTrailCount);
    }
    if (first >= kHangulBase && first < kHangulBase + kSyllableCount &&
        (first - kHangulBase) % kTrailCount == 0 && second > kTrailBase &&
        second < kTrailBase + kTrailCount) {
        return first + (second - kTrailBase);
    }
    const std::span<const Composition> compositions(kCompositions);
    const auto it =
        std::ranges::lower_bound(compositions, std::pair{first, second}, {},
                                 [](const Composition& c) { return std::pair{c.first, c.second}; });
    if (it != compositions.end() && it->first == first && it->second == second) {
        return it->composite;
    }
    return std::nullopt;
}

/// The canonical composition algorithm (UAX #15): each character joins the last starter unless a
/// character between them has a combining class at least as high.
void compose(std::u32string& text) {
    if (text.empty()) {
        return;
    }
    constexpr int kBlocked = 256; // a leading non-starter composes with nothing
    std::size_t starter = 0;
    int last_class = combining_class(text[0]) == 0 ? 0 : kBlocked;
    std::size_t out = 1;
    for (std::size_t i = 1; i < text.size(); ++i) {
        const char32_t cp = text[i];
        const int ccc = combining_class(cp);
        if (const auto composite = compose_pair(text[starter], cp);
            composite && (last_class < ccc || last_class == 0)) {
            text[starter] = *composite;
            continue;
        }
        if (ccc == 0) {
            starter = out;
        }
        last_class = ccc;
        text[out++] = cp;
    }
    text.resize(out);
}

} // namespace

std::optional<std::u32string> decode_utf8(std::string_view text) {
    std::u32string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<std::uint8_t>(text[i]);
        std::size_t length = 0;
        char32_t cp = 0;
        if (lead < 0x80U) {
            length = 1;
            cp = lead;
        } else if (lead >= 0xC2U && lead <= 0xDFU) {
            length = 2;
            cp = lead & 0x1FU;
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            length = 3;
            cp = lead & 0x0FU;
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            length = 4;
            cp = lead & 0x07U;
        } else {
            return std::nullopt;
        }
        if (i + length > text.size()) {
            return std::nullopt;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto next = static_cast<std::uint8_t>(text[i + k]);
            if ((next & 0xC0U) != 0x80U) {
                return std::nullopt;
            }
            cp = (cp << 6U) | (next & 0x3FU);
        }
        const bool overlong = (length == 3 && cp < 0x800) || (length == 4 && cp < 0x10000);
        if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return std::nullopt;
        }
        out.push_back(cp);
        i += length;
    }
    return out;
}

std::string encode_utf8(std::u32string_view code_points) {
    std::string out;
    out.reserve(code_points.size());
    for (const char32_t cp : code_points) {
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
    return out;
}

std::optional<std::string> to_nfc(std::string_view text) {
    const auto code_points = decode_utf8(text);
    if (!code_points) {
        return std::nullopt;
    }
    if (std::ranges::all_of(*code_points, [](char32_t cp) { return cp < kFirstUnstable; })) {
        return std::string(text);
    }
    std::u32string decomposed;
    decomposed.reserve(code_points->size() * 2);
    for (const char32_t cp : *code_points) {
        decompose(cp, decomposed);
    }
    reorder(decomposed);
    compose(decomposed);
    return encode_utf8(decomposed);
}

std::optional<std::string> fold_case(std::string_view text) {
    const auto normalized = to_nfc(text);
    if (!normalized) {
        return std::nullopt;
    }
    const auto code_points = decode_utf8(*normalized);
    if (!code_points) {
        return std::nullopt;
    }
    std::u32string folded = *code_points;
    for (char32_t& cp : folded) {
        if (const CaseMapping* mapping = find(kLowercase, cp)) {
            cp = mapping->lower;
        }
    }
    return encode_utf8(folded);
}

std::string_view unicode_version() {
    return kUnicodeVersion;
}

} // namespace osdrop::transfer
