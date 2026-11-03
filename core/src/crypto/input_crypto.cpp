#include "crypto/input_crypto.hpp"

#include "protocol/constants.hpp"

#include <limits>

namespace osdrop::crypto {
namespace {

using protocol::kInputHeaderSize;
using protocol::kMaxDatagramSize;
using protocol::kMaxInputPlaintextSize;
constexpr std::size_t kSequenceOffset = 4;
static_assert(kTagSize == protocol::kAeadTagSize);

std::uint64_t sequence_of(std::span<const std::uint8_t> datagram) {
    std::uint64_t sequence = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        sequence |= std::uint64_t{datagram[kSequenceOffset + i]} << (8U * i);
    }
    return sequence;
}

} // namespace

bool ReplayWindow::is_new(std::uint64_t sequence) const {
    if (!highest_ || sequence > *highest_) {
        return true;
    }
    const std::uint64_t age = *highest_ - sequence;
    return age < kSize && !seen_.test(static_cast<std::size_t>(age));
}

void ReplayWindow::accept(std::uint64_t sequence) {
    if (!highest_ || sequence > *highest_) {
        const std::uint64_t shift = highest_ ? sequence - *highest_ : kSize;
        if (shift >= kSize) {
            seen_.reset();
        } else {
            seen_ <<= static_cast<std::size_t>(shift);
        }
        seen_.set(0);
        highest_ = sequence;
        return;
    }
    const std::uint64_t age = *highest_ - sequence;
    if (age < kSize) {
        seen_.set(static_cast<std::size_t>(age));
    }
}

InputSealer::~InputSealer() {
    wipe(key_);
}

Expected<std::size_t, Error> InputSealer::seal(std::span<std::uint8_t> datagram,
                                               std::size_t packet_size) {
    using Result = Expected<std::size_t, Error>;
    if (packet_size < kInputHeaderSize || packet_size > kMaxInputPlaintextSize) {
        return Result::failure(Error::InvalidArgument);
    }
    if (datagram.size() < packet_size + kTagSize) {
        return Result::failure(Error::BufferTooSmall);
    }
    const std::uint64_t sequence = sequence_of(datagram);
    if (last_sequence_ && sequence <= *last_sequence_) {
        return Result::failure(Error::InvalidArgument);
    }
    if (sequence == std::numeric_limits<std::uint64_t>::max()) {
        return Result::failure(Error::NonceExhausted);
    }

    const auto header = datagram.first(kInputHeaderSize);
    const std::size_t body_size = packet_size - kInputHeaderSize;
    aead_encrypt(key_, sequence, header, datagram.subspan(kInputHeaderSize, body_size),
                 datagram.subspan(kInputHeaderSize, body_size + kTagSize));
    last_sequence_ = sequence;
    return packet_size + kTagSize;
}

InputOpener::~InputOpener() {
    wipe(key_);
}

Expected<std::span<const std::uint8_t>, Error> InputOpener::open(std::span<std::uint8_t> datagram) {
    using Result = Expected<std::span<const std::uint8_t>, Error>;
    if (datagram.size() < kInputHeaderSize + kTagSize) {
        return Result::failure(Error::Truncated);
    }
    if (datagram.size() > kMaxDatagramSize) {
        return Result::failure(Error::TooLarge);
    }
    const std::uint64_t sequence = sequence_of(datagram);
    if (!window_.is_new(sequence)) {
        return Result::failure(Error::Replayed);
    }

    const auto header = datagram.first(kInputHeaderSize);
    const std::size_t sealed_size = datagram.size() - kInputHeaderSize;
    if (!aead_decrypt(key_, sequence, header, datagram.subspan(kInputHeaderSize, sealed_size),
                      datagram.subspan(kInputHeaderSize, sealed_size - kTagSize))) {
        return Result::failure(Error::DecryptionFailed);
    }
    window_.accept(sequence);
    return std::span<const std::uint8_t>(datagram.first(datagram.size() - kTagSize));
}

} // namespace osdrop::crypto
