#include "protocol/advertisement.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <system_error>
#include <utility>

namespace osdrop::protocol {
namespace {

using Parsed = Expected<Advertisement, Error>;

constexpr std::string_view kHexDigits = "0123456789abcdef";

constexpr std::array<std::pair<Platform, std::string_view>, 2> kPlatformTexts{{
    {Platform::MacOS, "macos"},
    {Platform::Windows, "windows"},
}};

std::string_view platform_text(Platform platform) {
    for (const auto& [value, text] : kPlatformTexts) {
        if (value == platform) {
            return text;
        }
    }
    return {};
}

std::optional<Platform> platform_of(std::string_view text) {
    for (const auto& [value, known] : kPlatformTexts) {
        if (known == text) {
            return value;
        }
    }
    return std::nullopt;
}

/// A decimal number from `low` to `high`, without sign, spaces or leading zeros.
template <typename Number>
bool parse_number(std::string_view text, Number low, Number high, Number& out) {
    if (text.empty() || (text.size() > 1 && text.front() == '0')) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc{} && end == text.data() + text.size() && out >= low && out <= high;
}

std::string hex_text(const Id& bytes) {
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        text.push_back(kHexDigits.at(byte >> 4U));
        text.push_back(kHexDigits.at(byte & 0x0FU));
    }
    return text;
}

int hex_value(char c) {
    const auto position = kHexDigits.find(c);
    return position == std::string_view::npos ? -1 : static_cast<int>(position);
}

/// Exactly 32 lowercase hex digits.
std::optional<Id> parse_hex(std::string_view text) {
    Id bytes{};
    if (text.size() != bytes.size() * 2) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const int high = hex_value(text[i * 2]);
        const int low = hex_value(text[(i * 2) + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        bytes.at(i) = static_cast<std::uint8_t>((high << 4) | low);
    }
    return bytes;
}

} // namespace

Expected<TxtValues, Error> to_txt(const Advertisement& advertisement) {
    using Result = Expected<TxtValues, Error>;
    const bool group_ok = !advertisement.group_id || !is_zero(*advertisement.group_id);
    if (advertisement.protocol_version == 0 || platform_text(advertisement.platform).empty() ||
        !group_ok) {
        return Result::failure(Error::ValueOutOfRange);
    }
    if (const Check name = check_string(advertisement.name); !name) {
        return Result::failure(name.error());
    }
    return TxtValues{
        .protocol_version = std::to_string(advertisement.protocol_version),
        .protocol_minor = std::to_string(advertisement.protocol_minor),
        .device_id = hex_text(advertisement.device_id),
        .name = advertisement.name,
        .os = std::string(platform_text(advertisement.platform)),
        .group_id = advertisement.group_id ? hex_text(*advertisement.group_id) : std::string(),
    };
}

Expected<Advertisement, Error> from_txt(const TxtValues& values) {
    Advertisement advertisement;
    if (!parse_number<std::uint32_t>(values.protocol_version, 1,
                                     std::numeric_limits<std::uint32_t>::max(),
                                     advertisement.protocol_version) ||
        !parse_number<std::uint8_t>(values.protocol_minor, 0,
                                    std::numeric_limits<std::uint8_t>::max(),
                                    advertisement.protocol_minor)) {
        return Parsed::failure(Error::ValueOutOfRange);
    }
    const auto device_id = parse_hex(values.device_id);
    if (!device_id) {
        return Parsed::failure(Error::ValueOutOfRange);
    }
    advertisement.device_id = *device_id;
    if (const Check name = check_string(values.name); !name) {
        return Parsed::failure(name.error());
    }
    advertisement.name = values.name;
    const auto platform = platform_of(values.os);
    if (!platform) {
        return Parsed::failure(Error::ValueOutOfRange);
    }
    advertisement.platform = *platform;
    if (!values.group_id.empty()) {
        const auto group_id = parse_hex(values.group_id);
        if (!group_id || is_zero(*group_id)) {
            return Parsed::failure(Error::ValueOutOfRange);
        }
        advertisement.group_id = group_id;
    }
    return advertisement;
}

} // namespace osdrop::protocol
