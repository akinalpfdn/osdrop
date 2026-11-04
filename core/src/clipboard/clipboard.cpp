#include "clipboard/clipboard.hpp"

#include <algorithm>
#include <memory>
#include <tuple>
#include <utility>
#include <variant>

namespace osdrop::clipboard {
namespace {

using protocol::ClipboardFormat;
using protocol::ObjectKind;

constexpr std::uint32_t code_of(ClipboardFormat format) {
    return static_cast<std::uint32_t>(format);
}

bool is_content_format(ClipboardFormat format) {
    return format == ClipboardFormat::PlainText || format == ClipboardFormat::RichText ||
           format == ClipboardFormat::Html || format == ClipboardFormat::Image;
}

} // namespace

Clipboard::Clipboard(const DeviceId& self, ClipboardObserver& observer,
                     transfer::TransferObserver& transfer_observer, mesh::MeshObserver& next)
    : self_(self), observer_(observer), transfer_observer_(transfer_observer), next_(next) {}

void Clipboard::attach(mesh::Mesh& mesh, transfer::TransferService& transfers) {
    mesh_ = &mesh;
    transfers_ = &transfers;
}

void Clipboard::set_members(std::set<DeviceId> members) {
    members.erase(self_);
    members_ = std::move(members);
}

Expected<std::uint64_t, Error> Clipboard::copied(std::vector<LocalFormat> formats,
                                                 std::vector<std::filesystem::path> files,
                                                 std::int64_t wall_us) {
    using Result = Expected<std::uint64_t, Error>;
    if (mesh_ == nullptr || transfers_ == nullptr) {
        return Result::failure(Error::InvalidState);
    }
    std::ranges::sort(formats, {}, &LocalFormat::format);
    const bool repeated =
        std::ranges::adjacent_find(formats, {}, &LocalFormat::format) != formats.end();
    if ((formats.empty() && files.empty()) || repeated ||
        !std::ranges::all_of(formats,
                             [](const LocalFormat& f) { return is_content_format(f.format); }) ||
        !std::ranges::all_of(files, [](const auto& path) { return path.is_absolute(); })) {
        return Result::failure(Error::InvalidArgument);
    }
    withdraw_own();
    clip_id_ =
        std::max(static_cast<std::uint64_t>(std::max<std::int64_t>(wall_us, 1)), clip_id_ + 1);
    owner_ = self_;
    remote_.reset();
    waiting_.reset();

    std::vector<protocol::AnnouncedFormat> announced;
    for (LocalFormat& format : formats) {
        announced.push_back({.format = format.format, .byte_count = format.bytes.size()});
        const auto bytes =
            std::make_shared<const std::vector<std::uint8_t>>(std::move(format.bytes));
        static_cast<void>(transfers_->offer_content({.kind = ObjectKind::Clipboard,
                                                     .object_id = clip_id_,
                                                     .item_index = code_of(format.format)},
                                                    bytes, members_));
    }
    if (files.empty()) {
        announce(std::move(announced));
        return clip_id_;
    }
    const protocol::ObjectRef item{.kind = ObjectKind::Clipboard,
                                   .object_id = clip_id_,
                                   .item_index = protocol::kFilesClipboardFormat};
    if (const auto offered = transfers_->offer(item, std::move(files), members_); !offered) {
        return Result::failure(offered.error());
    }
    waiting_ = std::move(announced); // on_offer_ready() announces with the files' size
    return clip_id_;
}

void Clipboard::withdraw_own() {
    if (owner_ == self_ && clip_id_ != 0) {
        transfers_->withdraw(ObjectKind::Clipboard, clip_id_);
    }
}

void Clipboard::announce(std::vector<protocol::AnnouncedFormat> formats) {
    if (formats.empty()) {
        return; // nothing in the copy can travel
    }
    const protocol::ControlMessage message =
        protocol::ClipboardAnnounce{.clip_id = clip_id_, .formats = std::move(formats)};
    for (const DeviceId& device : members_) {
        mesh_->defer([device, message](mesh::Mesh& mesh, TimePoint now) {
            static_cast<void>(mesh.send_message(device, message, now)); // offline: it missed it
        });
    }
}

void Clipboard::receive_announce(const DeviceId& device,
                                 const protocol::ClipboardAnnounce& announce) {
    if (!members_.contains(device) ||
        std::tie(announce.clip_id, device) <= std::tie(clip_id_, owner_)) {
        return; // not shared with, or not newer than what this computer has
    }
    if (transfers_ != nullptr) {
        withdraw_own();
    }
    waiting_.reset();
    owner_ = device;
    clip_id_ = announce.clip_id;
    remote_ =
        RemoteClip{.device = device, .clip_id = announce.clip_id, .formats = announce.formats};
    observer_.on_remote_clip(*remote_);
}

// --- mesh::MeshObserver: ClipboardAnnounce stays here, the rest goes on ---

void Clipboard::on_message(const DeviceId& device, const protocol::ControlMessage& message) {
    if (const auto* announce = std::get_if<protocol::ClipboardAnnounce>(&message)) {
        receive_announce(device, *announce);
        return;
    }
    next_.on_message(device, message);
}

mesh::TrustVerdict Clipboard::on_trust_query(const crypto::Key& peer_key,
                                             const protocol::Hello& hello) {
    return next_.on_trust_query(peer_key, hello);
}

void Clipboard::on_pairing_code(mesh::ConnectionId connection, std::uint32_t code,
                                const crypto::Key& peer_key, const protocol::Hello& hello) {
    next_.on_pairing_code(connection, code, peer_key, hello);
}

void Clipboard::on_paired(const crypto::Key& peer_key, const protocol::Hello& hello) {
    next_.on_paired(peer_key, hello);
}

void Clipboard::on_pairing_failed(mesh::ConnectionId connection, Error reason) {
    next_.on_pairing_failed(connection, reason);
}

void Clipboard::on_peer_connected(const DeviceId& device, const crypto::Key& peer_key,
                                  const protocol::Hello& hello,
                                  const protocol::Capabilities& capabilities) {
    next_.on_peer_connected(device, peer_key, hello, capabilities);
}

void Clipboard::on_peer_disconnected(const DeviceId& device, std::optional<Error> reason,
                                     std::optional<protocol::ByeReason> bye) {
    next_.on_peer_disconnected(device, reason, bye);
}

void Clipboard::on_peer_capabilities(const DeviceId& device,
                                     const protocol::Capabilities& capabilities) {
    next_.on_peer_capabilities(device, capabilities);
}

void Clipboard::on_peer_responsive(const DeviceId& device, bool responsive) {
    next_.on_peer_responsive(device, responsive);
}

void Clipboard::on_motion(const DeviceId& device, const protocol::PointerMotion& motion) {
    next_.on_motion(device, motion);
}

void Clipboard::on_transfer_opened(const DeviceId& device, std::uint32_t stream_id) {
    next_.on_transfer_opened(device, stream_id);
}

void Clipboard::on_transfer_message(const DeviceId& device, std::uint32_t stream_id,
                                    const protocol::TransferMessage& message) {
    next_.on_transfer_message(device, stream_id, message);
}

void Clipboard::on_transfer_writable(const DeviceId& device, std::uint32_t stream_id) {
    next_.on_transfer_writable(device, stream_id);
}

void Clipboard::on_transfer_closed(const DeviceId& device, std::uint32_t stream_id,
                                   std::optional<Error> reason) {
    next_.on_transfer_closed(device, stream_id, reason);
}

void Clipboard::on_stats(const DeviceId& device, const mesh::SessionStats& stats) {
    next_.on_stats(device, stats);
}

void Clipboard::on_log(mesh::LogLevel level, std::string_view message) {
    next_.on_log(level, message);
}

// --- transfer::TransferObserver: a copy of files is announced once scanned ---

void Clipboard::on_offer_ready(const transfer::OfferReady& ready) {
    const bool own_files = ready.item.kind == ObjectKind::Clipboard &&
                           ready.item.object_id == clip_id_ && owner_ == self_ &&
                           ready.item.item_index == protocol::kFilesClipboardFormat;
    if (own_files && waiting_) {
        std::vector<protocol::AnnouncedFormat> formats = std::move(*waiting_);
        waiting_.reset();
        if (!ready.error) {
            formats.push_back({.format = ClipboardFormat::Files, .byte_count = ready.total_bytes});
        }
        announce(std::move(formats));
    }
    transfer_observer_.on_offer_ready(ready);
}

void Clipboard::on_transfer_progress(const transfer::TransferProgress& progress) {
    transfer_observer_.on_transfer_progress(progress);
}

void Clipboard::on_transfer_finished(const transfer::TransferOutcome& outcome) {
    transfer_observer_.on_transfer_finished(outcome);
}

void Clipboard::on_content_received(const transfer::ContentOutcome& outcome) {
    transfer_observer_.on_content_received(outcome);
}

} // namespace osdrop::clipboard
