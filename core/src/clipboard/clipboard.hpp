#pragma once

// The shared clipboard (DEVPLAN, Phase 15; docs/PROTOCOL.md, "0x40 ClipboardAnnounce"). A copy on
// a member is announced to the members this computer shares clipboards with: formats and sizes,
// never contents. Contents stay with the computer that copied them and are fetched over transfer
// streams by the members that want them.
//
// Every member keeps the newest copy it has heard of. Clip IDs are microseconds since 1970, and a
// copy takes an ID above every one this computer has seen; equal IDs are ordered by device ID. So
// members that hear of the same copies agree on the newest, and a copy made after hearing of
// another one always wins.
//
// The layer sits after the transfers in the mesh's observer chain: it keeps ClipboardAnnounce to
// itself and passes everything else on to `next`. It also stands between the transfers and their
// observer, to announce a copy of files once their scan gives the size.

#include "mesh/mesh.hpp"
#include "protocol/control_messages.hpp"
#include "transfer/service.hpp"
#include "transfer/types.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace osdrop::clipboard {

using mesh::DeviceId;

/// A copy on another member: what can be fetched from it.
struct RemoteClip {
    DeviceId device{};
    std::uint64_t clip_id = 0;
    std::vector<protocol::AnnouncedFormat> formats;
    bool operator==(const RemoteClip&) const = default;
};

/// One format of a copy on this computer (files are passed as paths instead).
struct LocalFormat {
    protocol::ClipboardFormat format = protocol::ClipboardFormat::PlainText;
    std::vector<std::uint8_t> bytes;
};

class ClipboardObserver {
public:
    virtual ~ClipboardObserver() = default;
    /// Another member's copy is now the newest: fetch what is wanted with the transfers
    /// (fetch_content for formats 1-4, receive for files).
    virtual void on_remote_clip(const RemoteClip& clip) = 0;
};

class Clipboard final : public mesh::MeshObserver, public transfer::TransferObserver {
public:
    Clipboard(const DeviceId& self, ClipboardObserver& observer,
              transfer::TransferObserver& transfer_observer, mesh::MeshObserver& next);
    ~Clipboard() override = default;
    Clipboard(const Clipboard&) = delete;
    Clipboard& operator=(const Clipboard&) = delete;
    Clipboard(Clipboard&&) = delete;
    Clipboard& operator=(Clipboard&&) = delete;

    /// The mesh to announce through and the transfers that serve contents; set once, before
    /// the mesh delivers anything.
    void attach(mesh::Mesh& mesh, transfer::TransferService& transfers);

    // --- Commands, on the mesh's thread; flush the mesh afterwards ---
    /// The members this computer shares clipboards with (their clipboard permission is on).
    void set_members(std::set<DeviceId> members);
    /// A copy on this computer: contents of formats 1-4 and paths for format 5. `wall_us` is the
    /// time in microseconds since 1970. The new clip's ID; INVALID_ARGUMENT for an empty copy, a
    /// repeated format or a relative path.
    [[nodiscard]] Expected<std::uint64_t, Error> copied(std::vector<LocalFormat> formats,
                                                        std::vector<std::filesystem::path> files,
                                                        std::int64_t wall_us);
    /// The newest copy when it is another member's.
    [[nodiscard]] const std::optional<RemoteClip>& remote() const { return remote_; }

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

private:
    void receive_announce(const DeviceId& device, const protocol::ClipboardAnnounce& announce);
    /// Withdraws this computer's offers for its copy, which another one replaced.
    void withdraw_own();
    void announce(std::vector<protocol::AnnouncedFormat> formats);

    DeviceId self_;
    ClipboardObserver& observer_;
    transfer::TransferObserver& transfer_observer_;
    mesh::MeshObserver& next_;
    mesh::Mesh* mesh_ = nullptr;                     // set by attach(); outlives this layer
    transfer::TransferService* transfers_ = nullptr; // likewise
    std::set<DeviceId> members_;                     ///< sharing clipboards, this computer excluded

    DeviceId owner_{};          ///< of the newest copy: self_ for a copy on this computer
    std::uint64_t clip_id_ = 0; ///< 0: no copy yet
    std::optional<RemoteClip> remote_;
    /// A copy of files on this computer waits for their scan before it is announced.
    std::optional<std::vector<protocol::AnnouncedFormat>> waiting_;
};

} // namespace osdrop::clipboard
