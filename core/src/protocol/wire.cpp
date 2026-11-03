#include "protocol/wire.hpp"

#include "protocol/text.hpp"

namespace osdrop::protocol {

std::span<const std::uint8_t> bytes_of(const std::string& text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

Check check_string(const std::string& text) {
    const bool valid =
        !text.empty() && text.size() <= kMaxStringBytes && is_valid_text(bytes_of(text));
    return valid ? ok() : fail(Error::InvalidString);
}

Check check_type_id(const std::string& text) {
    return is_valid_type_id(bytes_of(text)) ? ok() : fail(Error::InvalidString);
}

Check check_name(const std::string& text) {
    return is_valid_name(bytes_of(text)) ? ok() : fail(Error::InvalidString);
}

Check check_path(const std::string& text) {
    return is_valid_path(bytes_of(text)) ? ok() : fail(Error::InvalidString);
}

bool read_string(Reader& reader, std::string& out) {
    std::uint8_t length = 0;
    std::span<const std::uint8_t> raw;
    if (!reader.u8(length) || !reader.bytes(length, raw)) {
        return false;
    }
    out.assign(raw.begin(), raw.end());
    return true;
}

bool read_long_string(Reader& reader, std::string& out) {
    std::uint16_t length = 0;
    std::span<const std::uint8_t> raw;
    if (!reader.u16(length) || !reader.bytes(length, raw)) {
        return false;
    }
    out.assign(raw.begin(), raw.end());
    return true;
}

bool read_reserved(Reader& reader, std::size_t count, bool& all_zero) {
    std::span<const std::uint8_t> raw;
    if (!reader.bytes(count, raw)) {
        return false;
    }
    all_zero = all_zero && is_zero(raw);
    return true;
}

Check read_prefix(Reader& reader, std::uint8_t& type) {
    std::uint8_t magic0 = 0;
    std::uint8_t magic1 = 0;
    std::uint8_t version = 0;
    if (!reader.u8(magic0) || !reader.u8(magic1)) {
        return fail(Error::Truncated);
    }
    if (magic0 != kMagic[0] || magic1 != kMagic[1]) {
        return fail(Error::BadMagic);
    }
    if (!reader.u8(version)) {
        return fail(Error::Truncated);
    }
    if (version != kVersion) {
        return fail(Error::UnsupportedVersion);
    }
    return reader.u8(type) ? ok() : fail(Error::Truncated);
}

void write_prefix(Writer& writer, std::uint8_t type) {
    writer.bytes(kMagic);
    writer.u8(kVersion);
    writer.u8(type);
}

void write_string(Writer& writer, const std::string& text) {
    writer.u8(static_cast<std::uint8_t>(text.size()));
    writer.bytes(bytes_of(text));
}

void write_long_string(Writer& writer, const std::string& text) {
    writer.u16(static_cast<std::uint16_t>(text.size()));
    writer.bytes(bytes_of(text));
}

void write_reserved(Writer& writer, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        writer.u8(0);
    }
}

} // namespace osdrop::protocol
