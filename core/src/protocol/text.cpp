#include "protocol/text.hpp"

#include "protocol/constants.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace osdrop::protocol {

bool is_valid_text(std::span<const std::uint8_t> bytes) {
    constexpr std::array<std::uint32_t, 4> kSmallestForLength{0x0, 0x80, 0x800, 0x10000};
    std::size_t i = 0;
    while (i < bytes.size()) {
        const std::uint8_t lead = bytes[i];
        std::size_t extra = 0;
        std::uint32_t code_point = 0;
        if (lead < 0x80U) {
            code_point = lead;
        } else if ((lead & 0xE0U) == 0xC0U) {
            extra = 1;
            code_point = lead & 0x1FU;
        } else if ((lead & 0xF0U) == 0xE0U) {
            extra = 2;
            code_point = lead & 0x0FU;
        } else if ((lead & 0xF8U) == 0xF0U) {
            extra = 3;
            code_point = lead & 0x07U;
        } else {
            return false;
        }
        if (i + extra >= bytes.size() && extra > 0) {
            return false;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const std::uint8_t continuation = bytes[i + k];
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        const bool overlong = code_point < kSmallestForLength.at(extra);
        const bool surrogate = code_point >= 0xD800U && code_point <= 0xDFFFU;
        const bool control = code_point < 0x20U || (code_point >= 0x7FU && code_point < 0xA0U);
        if (overlong || surrogate || control || code_point > 0x10FFFFU) {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

bool is_valid_type_id(std::span<const std::uint8_t> bytes) {
    const auto allowed = [](std::uint8_t c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '.' || c == '-' || c == '_';
    };
    return !bytes.empty() && bytes.size() <= kMaxTypeIdBytes && std::ranges::all_of(bytes, allowed);
}

bool is_valid_name(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > kMaxNameBytes ||
        std::ranges::find(bytes, std::uint8_t{'/'}) != bytes.end()) {
        return false;
    }
    const bool dot = bytes.size() == 1 && bytes[0] == '.';
    const bool dot_dot = bytes.size() == 2 && bytes[0] == '.' && bytes[1] == '.';
    return !dot && !dot_dot && is_valid_text(bytes);
}

bool is_valid_path(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > kMaxPathBytes) {
        return false;
    }
    std::size_t start = 0;
    while (start <= bytes.size()) {
        const auto rest = bytes.subspan(start);
        const auto slash = std::ranges::find(rest, std::uint8_t{'/'});
        const auto length = static_cast<std::size_t>(slash - rest.begin());
        if (!is_valid_name(rest.first(length))) {
            return false;
        }
        start += length + 1;
    }
    return true;
}

} // namespace osdrop::protocol
