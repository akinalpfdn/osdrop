#include "mesh/transfer_channel.hpp"

#include "protocol/constants.hpp"

#include <variant>

namespace osdrop::mesh {

namespace {

/// Control types are below this, transfer types from it (PROTOCOL.md, "Transfer streams").
constexpr std::uint8_t kFirstTransferType = 0x80;
constexpr std::size_t kTypeOffset = 3;

} // namespace

TransferChannel::TransferChannel(std::uint32_t stream_id, const crypto::Key& send_key,
                                 const crypto::Key& receive_key)
    : stream_id_(stream_id), send_(send_key), receive_(receive_key) {}

Expected<std::vector<std::uint8_t>, Error>
TransferChannel::hello_frame(const protocol::Id& transfer_token) const {
    using Result = Expected<std::vector<std::uint8_t>, Error>;
    const auto hello = protocol::encode_transfer(
        protocol::TransferHello{.transfer_token = transfer_token, .stream_id = stream_id_});
    if (!hello) {
        return Result::failure(hello.error());
    }
    std::vector<std::uint8_t> frame;
    session::write_frame(hello.value(), frame);
    return frame;
}

Expected<std::vector<std::uint8_t>, Error>
TransferChannel::seal(const protocol::TransferMessage& message) {
    using Result = Expected<std::vector<std::uint8_t>, Error>;
    if (std::holds_alternative<protocol::TransferHello>(message)) {
        return Result::failure(Error::InvalidArgument); // only the plaintext first frame
    }
    const auto plaintext = protocol::encode_transfer(message);
    if (!plaintext) {
        return Result::failure(plaintext.error());
    }
    const auto ciphertext = send_.encrypt_with_ad({}, plaintext.value());
    if (!ciphertext) {
        return Result::failure(ciphertext.error());
    }
    std::vector<std::uint8_t> frame;
    session::write_frame(ciphertext.value(), frame);
    return frame;
}

Status<Error>
TransferChannel::receive(std::span<const std::uint8_t> bytes,
                         const std::function<void(const protocol::TransferMessage&)>& deliver) {
    frames_.append(bytes);
    for (;;) {
        const auto frame = frames_.next();
        if (!frame) {
            return Status<Error>::failure(frame.error());
        }
        if (frame.value().empty()) {
            return Status<Error>::ok();
        }
        const auto plaintext = receive_.decrypt_with_ad({}, frame.value());
        if (!plaintext) {
            return Status<Error>::failure(plaintext.error());
        }
        const auto message = protocol::decode_transfer(plaintext.value());
        if (!message) {
            return Status<Error>::failure(message.error());
        }
        if (std::holds_alternative<protocol::TransferHello>(message.value())) {
            return Status<Error>::failure(Error::UnexpectedMessage);
        }
        deliver(message.value());
    }
}

FirstFrame classify_first_frame(std::span<const std::uint8_t> bytes, std::size_t& frame_size) {
    if (bytes.size() < session::kFrameHeaderSize) {
        return FirstFrame::Incomplete;
    }
    std::uint32_t length = 0;
    for (std::size_t i = 0; i < session::kFrameHeaderSize; ++i) {
        length |= std::uint32_t{bytes[i]} << (8U * i);
    }
    if (length <= kTypeOffset || length > session::kMaxFramePayload) {
        return FirstFrame::Invalid;
    }
    if (bytes.size() < session::kFrameHeaderSize + length) {
        return FirstFrame::Incomplete;
    }
    frame_size = session::kFrameHeaderSize + length;
    const std::uint8_t type = bytes[session::kFrameHeaderSize + kTypeOffset];
    return type >= kFirstTransferType ? FirstFrame::Transfer : FirstFrame::Control;
}

} // namespace osdrop::mesh
