#include "protocol/advertisement.hpp"

#include <osdrop/osdrop.h>

#include <algorithm>
#include <iterator>
#include <span>
#include <string>

namespace {

namespace protocol = osdrop::protocol;

static_assert(OSDROP_DEVICE_ID_SIZE == protocol::kIdSize);
static_assert(OSDROP_GROUP_ID_SIZE == protocol::kIdSize);
static_assert(OSDROP_TXT_VALUE_SIZE == protocol::kMaxStringBytes + 1);
static_assert(OSDROP_PLATFORM_MACOS == static_cast<int>(protocol::Platform::MacOS) &&
              OSDROP_PLATFORM_WINDOWS == static_cast<int>(protocol::Platform::Windows));

/// A NUL-terminated value of at most OSDROP_TXT_VALUE_SIZE - 1 bytes; one without a NUL is read
/// whole and then fails the checks as too long.
std::string read_value(std::span<const char, OSDROP_TXT_VALUE_SIZE> value) {
    return {value.begin(), std::ranges::find(value, '\0')};
}

void write_value(const std::string& text, std::span<char, OSDROP_TXT_VALUE_SIZE> out) {
    const std::size_t length = std::min(text.size(), out.size() - 1);
    std::copy_n(text.data(), length, out.data());
    out[length] = '\0';
}

protocol::Id read_id(std::span<const std::uint8_t, protocol::kIdSize> bytes) {
    protocol::Id id{};
    std::ranges::copy(bytes, id.begin());
    return id;
}

} // namespace

extern "C" {

osdrop_result osdrop_advertisement_to_txt(const osdrop_advertisement* advertisement,
                                          osdrop_txt_record* out_record) {
    if (advertisement == nullptr || out_record == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    if ((advertisement->platform != OSDROP_PLATFORM_MACOS &&
         advertisement->platform != OSDROP_PLATFORM_WINDOWS) ||
        advertisement->has_group > 1) {
        return OSDROP_ERROR_VALUE_OUT_OF_RANGE;
    }
    protocol::Advertisement source{
        .protocol_version = advertisement->protocol_version,
        .protocol_minor = advertisement->protocol_minor,
        .device_id = read_id(advertisement->device_id),
        .name = read_value(advertisement->name),
        .platform = static_cast<protocol::Platform>(advertisement->platform),
        .group_id = std::nullopt,
    };
    if (advertisement->has_group == 1) {
        source.group_id = read_id(advertisement->group_id);
    }
    const auto values = protocol::to_txt(source);
    if (!values) {
        return static_cast<osdrop_result>(values.error());
    }
    *out_record = osdrop_txt_record{};
    write_value(values.value().protocol_version, out_record->protocol_version);
    write_value(values.value().protocol_minor, out_record->protocol_minor);
    write_value(values.value().device_id, out_record->device_id);
    write_value(values.value().name, out_record->name);
    write_value(values.value().os, out_record->os);
    write_value(values.value().group_id, out_record->group_id);
    return OSDROP_OK;
}

osdrop_result osdrop_advertisement_from_txt(const osdrop_txt_record* record,
                                            osdrop_advertisement* out_advertisement) {
    if (record == nullptr || out_advertisement == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const protocol::TxtValues values{
        .protocol_version = read_value(record->protocol_version),
        .protocol_minor = read_value(record->protocol_minor),
        .device_id = read_value(record->device_id),
        .name = read_value(record->name),
        .os = read_value(record->os),
        .group_id = read_value(record->group_id),
    };
    const auto parsed = protocol::from_txt(values);
    if (!parsed) {
        return static_cast<osdrop_result>(parsed.error());
    }
    const protocol::Advertisement& advertisement = parsed.value();
    osdrop_advertisement result{
        .protocol_version = advertisement.protocol_version,
        .platform = static_cast<osdrop_platform>(advertisement.platform),
        .protocol_minor = advertisement.protocol_minor,
        .has_group = static_cast<std::uint8_t>(advertisement.group_id.has_value()),
        .device_id = {},
        .group_id = {},
        .name = {},
    };
    std::ranges::copy(advertisement.device_id, std::begin(result.device_id));
    if (advertisement.group_id) {
        std::ranges::copy(*advertisement.group_id, std::begin(result.group_id));
    }
    write_value(advertisement.name, result.name);
    *out_advertisement = result;
    return OSDROP_OK;
}

} // extern "C"
