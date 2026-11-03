#include "protocol/byte_io.hpp"
#include "protocol/input_messages.hpp"
#include "protocol/wire.hpp"

#include <type_traits>
#include <vector>

namespace osdrop::protocol {
namespace {

using Decoded = Expected<InputDatagram, Error>;

Decoded decode_failure(Error error) {
    return Decoded::failure(error);
}

Check validate(const InputHello& m) {
    return is_zero(m.input_token) ? fail(Error::ValueOutOfRange) : ok();
}

Check validate(const PointerMotion& m) {
    return m.control_id != 0 && is_point(m.x, m.y) ? ok() : fail(Error::ValueOutOfRange);
}

Expected<InputMessage, Error> read_body(std::uint8_t type, Reader& r) {
    using Body = Expected<InputMessage, Error>;
    switch (static_cast<InputType>(type)) {
    case InputType::InputHello: {
        InputHello m;
        if (!read_array(r, m.input_token)) {
            return Body::failure(Error::Truncated);
        }
        return m;
    }
    case InputType::PointerMotion: {
        PointerMotion m;
        if (!r.u32(m.control_id) || !r.i32(m.x) || !r.i32(m.y) || !r.u64(m.capture_time_us)) {
            return Body::failure(Error::Truncated);
        }
        return m;
    }
    }
    return Body::failure(Error::UnknownType);
}

} // namespace

InputType type_of(const InputMessage& message) {
    return std::visit([](const auto& m) { return std::remove_cvref_t<decltype(m)>::kType; },
                      message);
}

Expected<InputDatagram, Error> decode_input(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > kMaxInputPlaintextSize) {
        return decode_failure(Error::TooLarge);
    }
    Reader reader(bytes);
    std::uint8_t type = 0;
    if (const Check prefix = read_prefix(reader, type); !prefix) {
        return decode_failure(prefix.error());
    }
    std::uint64_t sequence = 0;
    if (!reader.u64(sequence)) {
        return decode_failure(Error::Truncated);
    }
    auto body = read_body(type, reader);
    if (!body) {
        return decode_failure(body.error());
    }
    if (reader.remaining() != 0) {
        return decode_failure(Error::TrailingBytes);
    }
    const Check valid = std::visit([](const auto& m) { return validate(m); }, body.value());
    if (!valid) {
        return decode_failure(valid.error());
    }
    return InputDatagram{.sequence = sequence, .message = body.value()};
}

Expected<std::vector<std::uint8_t>, Error> encode_input(const InputDatagram& datagram) {
    using Result = Expected<std::vector<std::uint8_t>, Error>;
    const Check valid = std::visit([](const auto& m) { return validate(m); }, datagram.message);
    if (!valid) {
        return Result::failure(valid.error());
    }
    std::vector<std::uint8_t> buffer(kMaxInputPlaintextSize);
    Writer w(buffer);
    write_prefix(w, static_cast<std::uint8_t>(type_of(datagram.message)));
    w.u64(datagram.sequence);
    std::visit(
        [&](const auto& m) {
            using Message = std::remove_cvref_t<decltype(m)>;
            if constexpr (std::is_same_v<Message, InputHello>) {
                w.bytes(m.input_token);
            } else {
                w.u32(m.control_id);
                w.i32(m.x);
                w.i32(m.y);
                w.u64(m.capture_time_us);
            }
        },
        datagram.message);
    if (!w.ok()) {
        return Result::failure(Error::TooLarge);
    }
    buffer.resize(w.size());
    return buffer;
}

} // namespace osdrop::protocol
