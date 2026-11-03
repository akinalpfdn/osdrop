#include "session/control_framer.hpp"

#include <algorithm>

namespace osdrop::session {

void FrameDecoder::append(std::span<const std::uint8_t> bytes) {
    if (consumed_ > 0) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed_));
        consumed_ = 0;
    }
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

Expected<std::span<const std::uint8_t>, Error> FrameDecoder::next() {
    using Result = Expected<std::span<const std::uint8_t>, Error>;
    const std::size_t available = buffer_.size() - consumed_;
    if (available < kFrameHeaderSize) {
        return std::span<const std::uint8_t>{};
    }
    std::uint32_t length = 0;
    for (std::size_t i = 0; i < kFrameHeaderSize; ++i) {
        length |= std::uint32_t{buffer_[consumed_ + i]} << (8U * i);
    }
    if (length == 0 || length > kMaxFramePayload) {
        return Result::failure(Error::TooLarge);
    }
    if (available < kFrameHeaderSize + length) {
        return std::span<const std::uint8_t>{};
    }
    const auto payload = std::span(buffer_).subspan(consumed_ + kFrameHeaderSize, length);
    consumed_ += kFrameHeaderSize + length;
    return std::span<const std::uint8_t>(payload);
}

void write_frame(std::span<const std::uint8_t> payload, std::vector<std::uint8_t>& out) {
    out.resize(kFrameHeaderSize + payload.size());
    const auto length = static_cast<std::uint32_t>(payload.size());
    for (std::size_t i = 0; i < kFrameHeaderSize; ++i) {
        out[i] = static_cast<std::uint8_t>(length >> (8U * i));
    }
    std::ranges::copy(payload, out.begin() + kFrameHeaderSize);
}

} // namespace osdrop::session
