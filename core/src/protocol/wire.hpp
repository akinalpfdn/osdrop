#pragma once

// Field types shared by the control, input and transfer codecs (docs/PROTOCOL.md, "Conventions").

#include "protocol/byte_io.hpp"
#include "protocol/constants.hpp"
#include "protocol/error.hpp"
#include "util/expected.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace osdrop::protocol {

using Id = std::array<std::uint8_t, kIdSize>;
using PublicKey = std::array<std::uint8_t, kPublicKeySize>;
using Digest = std::array<std::uint8_t, kDigestSize>;
using Check = Status<Error>;

[[nodiscard]] inline Check ok() {
    return Check::ok();
}
[[nodiscard]] inline Check fail(Error error) {
    return Check::failure(error);
}

[[nodiscard]] inline bool in_range(std::uint64_t value, std::uint64_t low, std::uint64_t high) {
    return value >= low && value <= high;
}

[[nodiscard]] inline bool is_zero(std::span<const std::uint8_t> bytes) {
    return std::ranges::all_of(bytes, [](std::uint8_t byte) { return byte == 0; });
}

[[nodiscard]] inline bool is_coord(std::int32_t value) {
    return value >= -kMaxCoord && value <= kMaxCoord;
}
[[nodiscard]] inline bool is_point(std::int32_t x, std::int32_t y) {
    return is_coord(x) && is_coord(y);
}

[[nodiscard]] std::span<const std::uint8_t> bytes_of(const std::string& text);

/// `string`: 1–64 bytes of valid text.
[[nodiscard]] Check check_string(const std::string& text);
[[nodiscard]] Check check_type_id(const std::string& text);
[[nodiscard]] Check check_name(const std::string& text);
[[nodiscard]] Check check_path(const std::string& text);

// Readers return false when the input ends; the content is checked by validation afterwards.
[[nodiscard]] bool read_string(Reader& reader, std::string& out);      // u8 length
[[nodiscard]] bool read_long_string(Reader& reader, std::string& out); // u16 length
[[nodiscard]] bool read_reserved(Reader& reader, std::size_t count, bool& all_zero);

template <std::size_t Size>
[[nodiscard]] bool read_array(Reader& reader, std::array<std::uint8_t, Size>& out) {
    std::span<const std::uint8_t> raw;
    if (!reader.bytes(Size, raw)) {
        return false;
    }
    std::ranges::copy(raw, out.begin());
    return true;
}

/// Reads magic and version (Truncated, BadMagic, UnsupportedVersion) and the type byte.
[[nodiscard]] Check read_prefix(Reader& reader, std::uint8_t& type);
void write_prefix(Writer& writer, std::uint8_t type);

/// Reads a message body: fails (returns false) when the input ends, and remembers whether every
/// reserved field and reserved flag bit was 0 so the caller reports ValueOutOfRange afterwards.
class FieldReader {
public:
    explicit FieldReader(Reader& reader) : reader_(&reader) {}

    [[nodiscard]] Reader& raw() { return *reader_; }
    [[nodiscard]] bool reserved_zero() const { return reserved_zero_; }

    [[nodiscard]] bool reserved(std::size_t count) {
        return read_reserved(*reader_, count, reserved_zero_);
    }
    /// A flags byte whose bits outside `known` are reserved.
    [[nodiscard]] bool flags(std::uint8_t known, std::uint8_t& out) {
        if (!reader_->u8(out)) {
            return false;
        }
        reserved_zero_ = reserved_zero_ && (out & static_cast<std::uint8_t>(~known)) == 0;
        return true;
    }
    /// A u8 enumeration; its range is checked by validation.
    template <typename Enum>
    [[nodiscard]] bool enumeration(Enum& out) {
        std::uint8_t value = 0;
        if (!reader_->u8(value)) {
            return false;
        }
        out = static_cast<Enum>(value);
        return true;
    }

private:
    Reader* reader_;
    bool reserved_zero_ = true;
};

template <typename Enum>
[[nodiscard]] bool enum_in(Enum value, std::uint8_t low, std::uint8_t high) {
    return in_range(static_cast<std::uint8_t>(value), low, high);
}

[[nodiscard]] inline Check range(bool valid) {
    return valid ? ok() : fail(Error::ValueOutOfRange);
}

template <typename Enum>
void write_enum(Writer& writer, Enum value) {
    writer.u8(static_cast<std::uint8_t>(value));
}

void write_string(Writer& writer, const std::string& text);      // u8 length
void write_long_string(Writer& writer, const std::string& text); // u16 length
void write_reserved(Writer& writer, std::size_t count);

} // namespace osdrop::protocol
