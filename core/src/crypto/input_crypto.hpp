#pragma once

// Encryption of input datagrams (docs/PROTOCOL.md, "Encryption"): the 12-byte input header stays
// in clear as associated data, its sequence number is the nonce, the body is encrypted in place
// and followed by the tag.

#include "crypto/primitives.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace osdrop::crypto {

/// Accepts each sequence number once, in any order within the last kSize numbers.
class ReplayWindow {
public:
    static constexpr std::uint64_t kSize = 1024;

    [[nodiscard]] bool is_new(std::uint64_t sequence) const;
    /// Call only after the datagram authenticated, so forged packets cannot move the window.
    void accept(std::uint64_t sequence);

private:
    std::optional<std::uint64_t> highest_;
    std::bitset<kSize> seen_; // bit i: highest_ - i has been accepted
};

class InputSealer {
public:
    explicit InputSealer(const Key& key) : key_(key) {}
    InputSealer(const InputSealer& other) = delete;
    InputSealer& operator=(const InputSealer& other) = delete;
    InputSealer(InputSealer&& other) noexcept = default;
    InputSealer& operator=(InputSealer&& other) noexcept = default;
    ~InputSealer();

    /// `datagram` holds an encoded input datagram in its first `packet_size` bytes and has room
    /// for the tag. Sequence numbers must strictly increase: a repeated nonce would break the
    /// cipher.
    [[nodiscard]] Expected<std::size_t, Error> seal(std::span<std::uint8_t> datagram,
                                                    std::size_t packet_size);

private:
    Key key_;
    std::optional<std::uint64_t> last_sequence_;
};

class InputOpener {
public:
    explicit InputOpener(const Key& key) : key_(key) {}
    InputOpener(const InputOpener& other) = delete;
    InputOpener& operator=(const InputOpener& other) = delete;
    InputOpener(InputOpener&& other) noexcept = default;
    InputOpener& operator=(InputOpener&& other) noexcept = default;
    ~InputOpener();

    /// Authenticates and decrypts in place; returns the plaintext datagram (header and body) for
    /// protocol::decode_input. Replayed and forged datagrams are rejected.
    [[nodiscard]] Expected<std::span<const std::uint8_t>, Error>
    open(std::span<std::uint8_t> datagram);

private:
    Key key_;
    ReplayWindow window_;
};

} // namespace osdrop::crypto
