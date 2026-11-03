#pragma once

// TCP framing of control connections and transfer streams: u32 little-endian length, then the
// payload (docs/PROTOCOL.md, "Control channel").

#include "protocol/constants.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace osdrop::session {

/// Largest frame payload: a full control or transfer message plus the AEAD tag.
inline constexpr std::size_t kMaxFramePayload =
    protocol::kMaxControlMessageSize + protocol::kAeadTagSize;
static_assert(protocol::kMaxTransferMessageSize == protocol::kMaxControlMessageSize);
inline constexpr std::size_t kFrameHeaderSize = 4;

/// Reassembles frames from an arbitrary split of the TCP byte stream.
class FrameDecoder {
public:
    /// Appends bytes; call next() until it yields nothing.
    void append(std::span<const std::uint8_t> bytes);
    /// The next complete payload (valid until the next append), an empty span when more bytes are
    /// needed, or an error for an invalid length (which ends the session).
    [[nodiscard]] Expected<std::span<const std::uint8_t>, Error> next();

private:
    std::vector<std::uint8_t> buffer_;
    std::size_t consumed_ = 0;
};

/// Writes header and payload into `out` (resized to fit).
void write_frame(std::span<const std::uint8_t> payload, std::vector<std::uint8_t>& out);

} // namespace osdrop::session
