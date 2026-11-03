#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace osdrop::protocol {

/// Bounds-checked little-endian reader. Every read returns false instead of reading past the end.
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::size_t remaining() const { return bytes_.size() - offset_; }

    [[nodiscard]] bool u8(std::uint8_t& out) {
        if (remaining() < 1) {
            return false;
        }
        out = bytes_[offset_++];
        return true;
    }
    [[nodiscard]] bool u16(std::uint16_t& out) { return little_endian(out); }
    [[nodiscard]] bool u32(std::uint32_t& out) { return little_endian(out); }
    [[nodiscard]] bool u64(std::uint64_t& out) { return little_endian(out); }
    [[nodiscard]] bool i32(std::int32_t& out) {
        std::uint32_t raw = 0;
        if (!little_endian(raw)) {
            return false;
        }
        out = static_cast<std::int32_t>(raw);
        return true;
    }
    [[nodiscard]] bool i64(std::int64_t& out) {
        std::uint64_t raw = 0;
        if (!little_endian(raw)) {
            return false;
        }
        out = static_cast<std::int64_t>(raw);
        return true;
    }
    /// A view of the next `count` bytes; valid as long as the input buffer is.
    [[nodiscard]] bool bytes(std::size_t count, std::span<const std::uint8_t>& out) {
        if (remaining() < count) {
            return false;
        }
        out = bytes_.subspan(offset_, count);
        offset_ += count;
        return true;
    }

private:
    template <typename T>
    bool little_endian(T& out) {
        if (remaining() < sizeof(T)) {
            return false;
        }
        T value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            value = static_cast<T>(value | (static_cast<T>(bytes_[offset_ + i]) << (8U * i)));
        }
        offset_ += sizeof(T);
        out = value;
        return true;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

/// Bounds-checked little-endian writer into a caller-owned buffer; never allocates.
/// Writes past the end are dropped and make ok() false.
class Writer {
public:
    explicit Writer(std::span<std::uint8_t> out) : out_(out) {}

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] std::size_t size() const { return offset_; }

    void u8(std::uint8_t value) {
        if (reserve(1)) {
            out_[offset_++] = value;
        }
    }
    void u16(std::uint16_t value) { little_endian(value); }
    void u32(std::uint32_t value) { little_endian(value); }
    void u64(std::uint64_t value) { little_endian(value); }
    void i32(std::int32_t value) { little_endian(static_cast<std::uint32_t>(value)); }
    void i64(std::int64_t value) { little_endian(static_cast<std::uint64_t>(value)); }
    void bytes(std::span<const std::uint8_t> data) {
        if (reserve(data.size())) {
            for (const std::uint8_t byte : data) {
                out_[offset_++] = byte;
            }
        }
    }

private:
    bool reserve(std::size_t count) {
        if (!ok_ || out_.size() - offset_ < count) {
            ok_ = false;
            return false;
        }
        return true;
    }

    template <typename T>
    void little_endian(T value) {
        if (!reserve(sizeof(T))) {
            return;
        }
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            out_[offset_++] = static_cast<std::uint8_t>(value >> (8U * i));
        }
    }

    std::span<std::uint8_t> out_;
    std::size_t offset_ = 0;
    bool ok_ = true;
};

} // namespace osdrop::protocol
