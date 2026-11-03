#pragma once

// The transfer engine (DEVPLAN, Phase 13; docs/PROTOCOL.md, "Transfer streams"). It sits in the
// mesh's observer chain after the layout, so it hears only from members. Streams a peer opens are
// served by the Sender, streams the Receiver opens are its own; events of those stay here, and
// everything else goes on to `next`.
//
// Commands run on the mesh's thread. They queue their mesh calls as deferred work, so the caller
// flushes the mesh afterwards (Mesh::flush), as for the layout's commands.

#include "mesh/mesh.hpp"
#include "transfer/receiver.hpp"
#include "transfer/sender.hpp"
#include "transfer/streams.hpp"
#include "transfer/types.hpp"

#include <filesystem>
#include <set>
#include <string_view>
#include <vector>

namespace osdrop::transfer {

class TransferService final : public mesh::MeshObserver, private StreamsListener {
public:
    TransferService(TransferTunables tunables, TransferExecutor& executor,
                    TransferObserver& observer, mesh::MeshObserver& next);
    ~TransferService() override = default;
    TransferService(const TransferService&) = delete;
    TransferService& operator=(const TransferService&) = delete;
    TransferService(TransferService&&) = delete;
    TransferService& operator=(TransferService&&) = delete;

    /// The mesh to send through; set once, before it delivers anything.
    void attach(mesh::Mesh& mesh) { streams_.attach(mesh); }

    // --- Sending (Sender) ---
    [[nodiscard]] Status<Error> offer(const protocol::ObjectRef& item,
                                      std::vector<std::filesystem::path> roots,
                                      std::set<mesh::DeviceId> audience) {
        return sender_.offer(item, std::move(roots), std::move(audience));
    }
    void withdraw(protocol::ObjectKind kind, std::uint64_t object_id) {
        sender_.withdraw(kind, object_id);
    }

    // --- Receiving (Receiver) ---
    [[nodiscard]] Expected<TransferId, Error> receive(const mesh::DeviceId& device,
                                                      const protocol::ObjectRef& item,
                                                      const std::filesystem::path& destination) {
        return receiver_.receive(device, item, destination);
    }
    void cancel(TransferId id) { receiver_.cancel(id); }
    void set_files_permitted(std::set<mesh::DeviceId> devices) {
        receiver_.set_files_permitted(std::move(devices));
    }
    /// Quitting: transfers in progress are rolled back without a report.
    void shutdown() { receiver_.shutdown(); }

    // --- mesh::MeshObserver ---
    mesh::TrustVerdict on_trust_query(const crypto::Key& peer_key,
                                      const protocol::Hello& hello) override;
    void on_pairing_code(mesh::ConnectionId connection, std::uint32_t code,
                         const crypto::Key& peer_key, const protocol::Hello& hello) override;
    void on_paired(const crypto::Key& peer_key, const protocol::Hello& hello) override;
    void on_pairing_failed(mesh::ConnectionId connection, Error reason) override;
    void on_peer_connected(const mesh::DeviceId& device, const crypto::Key& peer_key,
                           const protocol::Hello& hello,
                           const protocol::Capabilities& capabilities) override;
    void on_peer_disconnected(const mesh::DeviceId& device, std::optional<Error> reason,
                              std::optional<protocol::ByeReason> bye) override;
    void on_peer_capabilities(const mesh::DeviceId& device,
                              const protocol::Capabilities& capabilities) override;
    void on_peer_responsive(const mesh::DeviceId& device, bool responsive) override;
    void on_message(const mesh::DeviceId& device, const protocol::ControlMessage& message) override;
    void on_motion(const mesh::DeviceId& device, const protocol::PointerMotion& motion) override;
    void on_transfer_opened(const mesh::DeviceId& device, std::uint32_t stream_id) override;
    void on_transfer_message(const mesh::DeviceId& device, std::uint32_t stream_id,
                             const protocol::TransferMessage& message) override;
    void on_transfer_writable(const mesh::DeviceId& device, std::uint32_t stream_id) override;
    void on_transfer_closed(const mesh::DeviceId& device, std::uint32_t stream_id,
                            std::optional<Error> reason) override;
    void on_stats(const mesh::DeviceId& device, const mesh::SessionStats& stats) override;
    void on_log(mesh::LogLevel level, std::string_view message) override;

private:
    void on_pumped(const StreamKey& key, std::size_t queued) override;

    TransferTunables tunables_;
    Streams streams_;
    Sender sender_;
    Receiver receiver_;
    mesh::MeshObserver& next_;
};

} // namespace osdrop::transfer
