#pragma once

// One transfer stream's framing and encryption, without I/O (docs/PROTOCOL.md, "Transfer streams"
// and "Encryption"): the opener's first frame is a plaintext TransferHello, every later frame in
// either direction is a transfer message sealed with that direction's stream key.

#include "crypto/noise.hpp"
#include "protocol/transfer_messages.hpp"
#include "session/control_framer.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace osdrop::mesh {

class TransferChannel {
public:
    /// `send_key` protects what this side sends, `receive_key` what the peer sends.
    TransferChannel(std::uint32_t stream_id, const crypto::Key& send_key,
                    const crypto::Key& receive_key);

    [[nodiscard]] std::uint32_t stream_id() const { return stream_id_; }

    /// The opener's first frame: TransferHello with the token the peer issued, in plaintext.
    [[nodiscard]] Expected<std::vector<std::uint8_t>, Error>
    hello_frame(const protocol::Id& transfer_token) const;

    /// One sealed, framed transfer message, ready to write.
    [[nodiscard]] Expected<std::vector<std::uint8_t>, Error>
    seal(const protocol::TransferMessage& message);

    /// Feeds received bytes; calls `deliver` for each complete message. Fails, and must then be
    /// closed, on a malformed, altered, replayed or misplaced frame (a second TransferHello).
    [[nodiscard]] Status<Error>
    receive(std::span<const std::uint8_t> bytes,
            const std::function<void(const protocol::TransferMessage&)>& deliver);

private:
    std::uint32_t stream_id_;
    crypto::CipherState send_;
    crypto::CipherState receive_;
    session::FrameDecoder frames_;
};

/// The first frame of an accepted connection decides what it is: a control connection or a
/// transfer stream (PROTOCOL.md: transfer types never overlap control types).
enum class FirstFrame : std::uint8_t { Incomplete, Control, Transfer, Invalid };

/// Classifies `bytes`, the start of an accepted connection, and gives the size of its first
/// frame (header and payload) once it is complete.
[[nodiscard]] FirstFrame classify_first_frame(std::span<const std::uint8_t> bytes,
                                              std::size_t& frame_size);

} // namespace osdrop::mesh
