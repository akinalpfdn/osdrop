#pragma once

// Input datagrams (docs/PROTOCOL.md, "Input datagrams"), in plaintext: the 12-byte header followed
// by the body. On the wire the body is encrypted and the header is the associated data.

#include "protocol/constants.hpp"
#include "protocol/error.hpp"
#include "protocol/wire.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace osdrop::protocol {

enum class InputType : std::uint8_t {
    InputHello = 0x01,
    PointerMotion = 0x02,
};

struct InputHello {
    static constexpr InputType kType = InputType::InputHello;
    Id input_token{};
    bool operator==(const InputHello&) const = default;
};

struct PointerMotion {
    static constexpr InputType kType = InputType::PointerMotion;
    std::uint32_t control_id = 0;
    std::int32_t x = 0; ///< 1/16 point, the endpoint's desktop space
    std::int32_t y = 0;
    std::uint64_t capture_time_us = 0;
    bool operator==(const PointerMotion&) const = default;
};

using InputMessage = std::variant<InputHello, PointerMotion>;

struct InputDatagram {
    std::uint64_t sequence = 0;
    InputMessage message;
    bool operator==(const InputDatagram&) const = default;
};

[[nodiscard]] InputType type_of(const InputMessage& message);

/// Decodes a plaintext datagram: header and body.
[[nodiscard]] Expected<InputDatagram, Error> decode_input(std::span<const std::uint8_t> bytes);

[[nodiscard]] Expected<std::vector<std::uint8_t>, Error>
encode_input(const InputDatagram& datagram);

} // namespace osdrop::protocol
