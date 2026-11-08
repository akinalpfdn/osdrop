#include <osdrop/win/txt_mapping.hpp>

#include <algorithm>
#include <span>
#include <string_view>

namespace osdrop::win {
namespace {

void copy_value(const std::string& value, std::span<char, OSDROP_TXT_VALUE_SIZE> out) {
    const std::size_t length = std::min(value.size(), out.size() - 1);
    std::copy_n(value.begin(), length, out.begin());
    out[length] = '\0';
}

std::string read(std::span<const char, OSDROP_TXT_VALUE_SIZE> value) {
    return {value.begin(), std::ranges::find(value, '\0')};
}

} // namespace

TxtPairs txt_pairs(const osdrop_txt_record& record) {
    return {{OSDROP_TXT_KEY_PROTOCOL_VERSION, read(record.protocol_version)},
            {OSDROP_TXT_KEY_PROTOCOL_MINOR, read(record.protocol_minor)},
            {OSDROP_TXT_KEY_DEVICE_ID, read(record.device_id)},
            {OSDROP_TXT_KEY_NAME, read(record.name)},
            {OSDROP_TXT_KEY_OS, read(record.os)},
            {OSDROP_TXT_KEY_GROUP_ID, read(record.group_id)}};
}

osdrop_txt_record txt_record(const TxtPairs& pairs) {
    osdrop_txt_record record{};
    for (const auto& [key, value] : pairs) {
        if (key == OSDROP_TXT_KEY_PROTOCOL_VERSION) {
            copy_value(value, record.protocol_version);
        } else if (key == OSDROP_TXT_KEY_PROTOCOL_MINOR) {
            copy_value(value, record.protocol_minor);
        } else if (key == OSDROP_TXT_KEY_DEVICE_ID) {
            copy_value(value, record.device_id);
        } else if (key == OSDROP_TXT_KEY_NAME) {
            copy_value(value, record.name);
        } else if (key == OSDROP_TXT_KEY_OS) {
            copy_value(value, record.os);
        } else if (key == OSDROP_TXT_KEY_GROUP_ID) {
            copy_value(value, record.group_id);
        }
    }
    return record;
}

std::string instance_label(const osdrop_txt_record& record) {
    return read(record.device_id);
}

std::optional<std::string> label_of(const std::string& instance_name) {
    constexpr std::string_view kSuffix = "._osdrop._tcp.local";
    std::string_view name = instance_name;
    if (name.ends_with('.')) {
        name.remove_suffix(1);
    }
    if (name.size() <= kSuffix.size() || !name.ends_with(kSuffix)) {
        return std::nullopt;
    }
    return std::string(name.substr(0, name.size() - kSuffix.size()));
}

} // namespace osdrop::win
