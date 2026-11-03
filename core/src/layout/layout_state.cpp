#include "layout/layout_state.hpp"

#include "protocol/byte_io.hpp"
#include "protocol/wire.hpp"

#include <array>
#include <utility>
#include <variant>

namespace osdrop::layout {

namespace {

// "ODLY", version, then (integers little-endian):
//   group_id bytes[16]
//   document_length u16, then the document as an encoded LayoutState message (absent when 0)
//   screen_count u16, screen × count: device_id[16], length u16, an encoded ScreenReport message
// Embedding the wire messages reuses their strict checks.
constexpr std::array<std::uint8_t, 4> kMagic{'O', 'D', 'L', 'Y'};

using Decoded = Expected<LayoutState, Error>;
using Bytes = std::vector<std::uint8_t>;

/// The next `u16 length` and that many bytes, decoded as a control message of type T.
template <typename T>
Expected<T, Error> read_embedded(protocol::Reader& reader, std::uint16_t length) {
    std::span<const std::uint8_t> bytes;
    if (!reader.bytes(length, bytes)) {
        return Expected<T, Error>::failure(Error::Truncated);
    }
    auto message = protocol::decode_control(bytes);
    if (!message) {
        return Expected<T, Error>::failure(message.error());
    }
    if (auto* typed = std::get_if<T>(&message.value())) {
        return std::move(*typed);
    }
    return Expected<T, Error>::failure(Error::ValueOutOfRange);
}

} // namespace

Bytes encode_layout(const LayoutState& state) {
    Bytes document;
    if (!state.document.placements.empty()) {
        if (auto encoded = protocol::encode_control(state.document.to_message())) {
            document = std::move(encoded.value());
        }
    }
    std::vector<std::pair<DeviceId, Bytes>> screens;
    for (const auto& [device, desktop] : state.screens) {
        if (desktop.empty()) {
            continue;
        }
        if (auto encoded = protocol::encode_control(desktop.to_report())) {
            screens.emplace_back(device, std::move(encoded.value()));
        }
    }
    std::size_t size = kMagic.size() + 1 + protocol::kIdSize + 2 + document.size() + 2;
    for (const auto& [device, report] : screens) {
        size += protocol::kIdSize + 2 + report.size();
    }
    Bytes bytes(size);
    protocol::Writer writer(bytes);
    writer.bytes(kMagic);
    writer.u8(kLayoutStateVersion);
    writer.bytes(state.group_id);
    writer.u16(static_cast<std::uint16_t>(document.size()));
    writer.bytes(document);
    writer.u16(static_cast<std::uint16_t>(screens.size()));
    for (const auto& [device, report] : screens) {
        writer.bytes(device);
        writer.u16(static_cast<std::uint16_t>(report.size()));
        writer.bytes(report);
    }
    return bytes;
}

Decoded decode_layout(std::span<const std::uint8_t> bytes) {
    protocol::Reader reader(bytes);
    std::array<std::uint8_t, kMagic.size()> magic{};
    std::uint8_t version = 0;
    if (!protocol::read_array(reader, magic) || !reader.u8(version)) {
        return Decoded::failure(Error::Truncated);
    }
    if (magic != kMagic) {
        return Decoded::failure(Error::BadMagic);
    }
    if (version != kLayoutStateVersion) {
        return Decoded::failure(Error::UnsupportedVersion);
    }
    LayoutState state;
    std::uint16_t length = 0;
    if (!protocol::read_array(reader, state.group_id) || !reader.u16(length)) {
        return Decoded::failure(Error::Truncated);
    }
    if (length > 0) {
        auto document = read_embedded<protocol::LayoutState>(reader, length);
        if (!document) {
            return Decoded::failure(document.error());
        }
        state.document = LayoutDocument::from_message(document.value());
    }
    std::uint16_t count = 0;
    if (!reader.u16(count)) {
        return Decoded::failure(Error::Truncated);
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        DeviceId device{};
        if (!protocol::read_array(reader, device) || !reader.u16(length)) {
            return Decoded::failure(Error::Truncated);
        }
        auto report = read_embedded<protocol::ScreenReport>(reader, length);
        if (!report) {
            return Decoded::failure(report.error());
        }
        if (!state.screens.emplace(device, Screens::from_report(report.value())).second) {
            return Decoded::failure(Error::ValueOutOfRange);
        }
    }
    if (reader.remaining() != 0) {
        return Decoded::failure(Error::TrailingBytes);
    }
    return state;
}

} // namespace osdrop::layout
