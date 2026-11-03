#include "transfer/service.hpp"

namespace osdrop::transfer {

TransferService::TransferService(TransferTunables tunables, TransferExecutor& executor,
                                 TransferObserver& observer, mesh::MeshObserver& next)
    : tunables_(tunables), streams_(*this), sender_(tunables_, streams_, executor, observer),
      receiver_(tunables_, streams_, executor, observer), next_(next) {}

mesh::TrustVerdict TransferService::on_trust_query(const crypto::Key& peer_key,
                                                   const protocol::Hello& hello) {
    return next_.on_trust_query(peer_key, hello);
}

void TransferService::on_pairing_code(mesh::ConnectionId connection, std::uint32_t code,
                                      const crypto::Key& peer_key, const protocol::Hello& hello) {
    next_.on_pairing_code(connection, code, peer_key, hello);
}

void TransferService::on_paired(const crypto::Key& peer_key, const protocol::Hello& hello) {
    next_.on_paired(peer_key, hello);
}

void TransferService::on_pairing_failed(mesh::ConnectionId connection, Error reason) {
    next_.on_pairing_failed(connection, reason);
}

void TransferService::on_peer_connected(const mesh::DeviceId& device, const crypto::Key& peer_key,
                                        const protocol::Hello& hello,
                                        const protocol::Capabilities& capabilities) {
    receiver_.peer_connected(device, capabilities.max_transfer_streams);
    next_.on_peer_connected(device, peer_key, hello, capabilities);
}

void TransferService::on_peer_disconnected(const mesh::DeviceId& device,
                                           std::optional<Error> reason,
                                           std::optional<protocol::ByeReason> bye) {
    receiver_.peer_disconnected(device);
    next_.on_peer_disconnected(device, reason, bye);
}

void TransferService::on_peer_capabilities(const mesh::DeviceId& device,
                                           const protocol::Capabilities& capabilities) {
    receiver_.peer_connected(device, capabilities.max_transfer_streams);
    next_.on_peer_capabilities(device, capabilities);
}

void TransferService::on_peer_responsive(const mesh::DeviceId& device, bool responsive) {
    next_.on_peer_responsive(device, responsive);
}

void TransferService::on_message(const mesh::DeviceId& device,
                                 const protocol::ControlMessage& message) {
    next_.on_message(device, message);
}

void TransferService::on_motion(const mesh::DeviceId& device,
                                const protocol::PointerMotion& motion) {
    next_.on_motion(device, motion);
}

void TransferService::on_transfer_opened(const mesh::DeviceId& device, std::uint32_t stream_id) {
    sender_.opened({.device = device, .id = stream_id});
}

void TransferService::on_transfer_message(const mesh::DeviceId& device, std::uint32_t stream_id,
                                          const protocol::TransferMessage& message) {
    const StreamKey key{.device = device, .id = stream_id};
    if (receiver_.owns(key)) {
        receiver_.message(key, message);
    } else if (sender_.serves(key)) {
        sender_.message(key, message);
    } else {
        next_.on_transfer_message(device, stream_id, message);
    }
}

void TransferService::on_transfer_writable(const mesh::DeviceId& device, std::uint32_t stream_id) {
    const StreamKey key{.device = device, .id = stream_id};
    if (receiver_.owns(key)) {
        streams_.writable(key);
        receiver_.writable(key);
    } else if (sender_.serves(key)) {
        streams_.writable(key);
    } else {
        next_.on_transfer_writable(device, stream_id);
    }
}

void TransferService::on_transfer_closed(const mesh::DeviceId& device, std::uint32_t stream_id,
                                         std::optional<Error> reason) {
    const StreamKey key{.device = device, .id = stream_id};
    streams_.closed(key);
    if (receiver_.owns(key)) {
        receiver_.closed(key, reason);
    } else if (sender_.serves(key)) {
        sender_.closed(key);
    } else {
        next_.on_transfer_closed(device, stream_id, reason);
    }
}

void TransferService::on_stats(const mesh::DeviceId& device, const mesh::SessionStats& stats) {
    next_.on_stats(device, stats);
}

void TransferService::on_log(mesh::LogLevel level, std::string_view message) {
    next_.on_log(level, message);
}

void TransferService::on_pumped(const StreamKey& key, std::size_t queued) {
    if (!receiver_.owns(key)) {
        sender_.pumped(key, queued);
    }
}

} // namespace osdrop::transfer
