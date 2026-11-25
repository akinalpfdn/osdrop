#pragma once

// Files sent to one member without a drag (DEVPLAN, Phases 20 and 21; docs/PROTOCOL.md, "Sending
// files"): a phone sends what was shared to it, a computer what was dropped on a phone's corner.
// The sender offers the items to that member alone and, once they are scanned, sends a SendOffer;
// the member fetches them as object kind 3 and answers with a SendResult. The sender withdraws the
// items then, or when the session ends.
//
// The layer sits after the transfers in the mesh's observer chain: it keeps SendOffer and
// SendResult to itself and passes everything else on to `next`. It also stands between the
// transfers and their observer, to send an offer once its items are scanned.

#include "mesh/mesh.hpp"
#include "protocol/control_messages.hpp"
#include "transfer/service.hpp"
#include "transfer/types.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace osdrop::sending {

using mesh::DeviceId;

/// A file or folder to send, with its type (a Uniform Type Identifier; replaced when invalid).
struct SentPath {
    std::filesystem::path path;
    std::string content_type;
};

/// Files another member sent this computer: fetch each item with the transfers (object kind 3,
/// object ID `send_id`, item index its place), then answer().
struct ArrivedSend {
    DeviceId device{};
    std::uint64_t send_id = 0;
    std::vector<protocol::OfferedItem> items;
    bool operator==(const ArrivedSend&) const = default;
};

class SendsObserver {
public:
    virtual ~SendsObserver() = default;
    virtual void on_send_arrived(const ArrivedSend& send) = 0;
    /// The end of a send from this computer: the member's answer, or Failed when the items could
    /// not be offered or the session ended first.
    virtual void on_send_finished(const DeviceId& device, std::uint64_t send_id,
                                  protocol::SendOutcome outcome) = 0;
};

class Sends final : public mesh::MeshObserver, public transfer::TransferObserver {
public:
    Sends(const DeviceId& self, SendsObserver& observer,
          transfer::TransferObserver& transfer_observer, mesh::MeshObserver& next);
    ~Sends() override = default;
    Sends(const Sends&) = delete;
    Sends& operator=(const Sends&) = delete;
    Sends(Sends&&) = delete;
    Sends& operator=(Sends&&) = delete;

    /// The mesh to send through and the transfers that serve the items; set once, before the mesh
    /// delivers anything.
    void attach(mesh::Mesh& mesh, transfer::TransferService& transfers);

    // --- Commands, on the mesh's thread; flush the mesh afterwards ---
    /// The members that may send this computer files (their files permission is on).
    void set_file_senders(std::set<DeviceId> devices) { file_senders_ = std::move(devices); }
    /// Sends `items` to the connected member `device`: the new send's ID. INVALID_ARGUMENT for no
    /// items, more than kMaxDragItems or a relative path; INVALID_STATE when the member is not
    /// connected; UNSUPPORTED_VERSION when it speaks protocol minor 0.
    [[nodiscard]] Expected<std::uint64_t, Error> send(const DeviceId& device,
                                                      std::vector<SentPath> items);
    /// Answers a send that arrived, once its items were fetched or not wanted. NOT_FOUND for a
    /// send that is not waiting for an answer.
    [[nodiscard]] Status<Error> answer(const DeviceId& device, std::uint64_t send_id,
                                       protocol::SendOutcome outcome);

    // --- mesh::MeshObserver ---
    mesh::TrustVerdict on_trust_query(const crypto::Key& peer_key,
                                      const protocol::Hello& hello) override;
    void on_pairing_code(mesh::ConnectionId connection, std::uint32_t code,
                         const crypto::Key& peer_key, const protocol::Hello& hello) override;
    void on_paired(const crypto::Key& peer_key, const protocol::Hello& hello) override;
    void on_pairing_failed(mesh::ConnectionId connection, Error reason) override;
    void on_peer_connected(const DeviceId& device, const crypto::Key& peer_key,
                           const protocol::Hello& hello,
                           const protocol::Capabilities& capabilities) override;
    void on_peer_disconnected(const DeviceId& device, std::optional<Error> reason,
                              std::optional<protocol::ByeReason> bye) override;
    void on_peer_capabilities(const DeviceId& device,
                              const protocol::Capabilities& capabilities) override;
    void on_peer_responsive(const DeviceId& device, bool responsive) override;
    void on_message(const DeviceId& device, const protocol::ControlMessage& message) override;
    void on_motion(const DeviceId& device, const protocol::PointerMotion& motion) override;
    void on_transfer_opened(const DeviceId& device, std::uint32_t stream_id) override;
    void on_transfer_message(const DeviceId& device, std::uint32_t stream_id,
                             const protocol::TransferMessage& message) override;
    void on_transfer_writable(const DeviceId& device, std::uint32_t stream_id) override;
    void on_transfer_closed(const DeviceId& device, std::uint32_t stream_id,
                            std::optional<Error> reason) override;
    void on_stats(const DeviceId& device, const mesh::SessionStats& stats) override;
    void on_log(mesh::LogLevel level, std::string_view message) override;

    // --- transfer::TransferObserver ---
    void on_offer_ready(const transfer::OfferReady& ready) override;
    void on_transfer_progress(const transfer::TransferProgress& progress) override;
    void on_transfer_finished(const transfer::TransferOutcome& outcome) override;
    void on_content_received(const transfer::ContentOutcome& outcome) override;
    void on_send_progress(const transfer::SendProgress& progress) override;
    void on_transfer_planned(const transfer::TransferPlan& plan) override;
    void on_entry_written(const transfer::EntryWritten& written) override;

private:
    /// A send from this computer: offered, scanned, then waiting for the member's answer.
    struct Outgoing {
        DeviceId device{};
        std::vector<protocol::OfferedItem> items;
        std::size_t scanning = 0; ///< items not scanned yet
        bool broken = false;      ///< an item cannot travel
    };

    void receive_offer(const DeviceId& device, const protocol::SendOffer& offer);
    void receive_result(const DeviceId& device, const protocol::SendResult& result);
    /// Withdraws the send's items and tells the observer how it ended.
    void finish(std::uint64_t send_id, protocol::SendOutcome outcome);
    void send_message(const DeviceId& device, protocol::ControlMessage message);

    DeviceId self_;
    SendsObserver& observer_;
    transfer::TransferObserver& transfer_observer_;
    mesh::MeshObserver& next_;
    mesh::Mesh* mesh_ = nullptr;                     // set by attach(); outlives this layer
    transfer::TransferService* transfers_ = nullptr; // likewise

    std::set<DeviceId> file_senders_;
    std::map<DeviceId, std::uint8_t> minors_; ///< connected members' protocol minor versions
    std::uint64_t last_send_id_ = 0;
    std::map<std::uint64_t, Outgoing> outgoing_;
    std::set<std::pair<DeviceId, std::uint64_t>> incoming_; ///< waiting for this computer's answer
};

} // namespace osdrop::sending
