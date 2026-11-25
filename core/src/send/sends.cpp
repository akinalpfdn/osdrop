#include "send/sends.hpp"

#include "protocol/constants.hpp"
#include "protocol/text.hpp"

#include <algorithm>
#include <variant>

namespace osdrop::sending {
namespace {

using protocol::ObjectKind;
using protocol::SendOutcome;

constexpr std::string_view kFolderType = "public.folder";
constexpr std::string_view kDataType = "public.data";

} // namespace

Sends::Sends(const DeviceId& self, SendsObserver& observer,
             transfer::TransferObserver& transfer_observer, mesh::MeshObserver& next)
    : self_(self), observer_(observer), transfer_observer_(transfer_observer), next_(next) {}

void Sends::attach(mesh::Mesh& mesh, transfer::TransferService& transfers) {
    mesh_ = &mesh;
    transfers_ = &transfers;
}

Expected<std::uint64_t, Error> Sends::send(const DeviceId& device, std::vector<SentPath> items) {
    using Result = Expected<std::uint64_t, Error>;
    if (items.empty() || items.size() > protocol::kMaxDragItems ||
        !std::ranges::all_of(items, [](const SentPath& item) { return item.path.is_absolute(); })) {
        return Result::failure(Error::InvalidArgument);
    }
    const auto minor = minors_.find(device);
    if (mesh_ == nullptr || transfers_ == nullptr || device == self_ || minor == minors_.end()) {
        return Result::failure(Error::InvalidState);
    }
    if (minor->second < 1) {
        return Result::failure(Error::UnsupportedVersion); // it would not understand a SendOffer
    }
    const std::uint64_t send_id = ++last_send_id_;
    Outgoing& outgoing = outgoing_[send_id];
    outgoing.device = device;
    outgoing.items.resize(items.size());
    outgoing.scanning = items.size();
    for (std::uint32_t index = 0; index < items.size(); ++index) {
        outgoing.items[index].content_type = std::move(items[index].content_type);
        const protocol::ObjectRef item{
            .kind = ObjectKind::Sent, .object_id = send_id, .item_index = index};
        if (!transfers_->offer(item, {std::move(items[index].path)}, {device})) {
            outgoing.broken = true; // its paths are absolute, so this does not happen
        }
    }
    return send_id;
}

Status<Error> Sends::answer(const DeviceId& device, std::uint64_t send_id, SendOutcome outcome) {
    if (incoming_.erase({device, send_id}) == 0) {
        return Status<Error>::failure(Error::NotFound);
    }
    send_message(device, protocol::SendResult{.send_id = send_id, .outcome = outcome});
    return Status<Error>::ok();
}

void Sends::receive_offer(const DeviceId& device, const protocol::SendOffer& offer) {
    if (!file_senders_.contains(device) || incoming_.contains({device, offer.send_id})) {
        send_message(device, protocol::SendResult{.send_id = offer.send_id,
                                                  .outcome = SendOutcome::Declined});
        return;
    }
    incoming_.insert({device, offer.send_id});
    observer_.on_send_arrived(
        ArrivedSend{.device = device, .send_id = offer.send_id, .items = offer.items});
}

void Sends::receive_result(const DeviceId& device, const protocol::SendResult& result) {
    const auto outgoing = outgoing_.find(result.send_id);
    if (outgoing == outgoing_.end() || outgoing->second.device != device ||
        outgoing->second.scanning > 0) {
        return; // not a send of this computer's to that member, or not offered yet
    }
    finish(result.send_id, result.outcome);
}

void Sends::finish(std::uint64_t send_id, SendOutcome outcome) {
    const auto outgoing = outgoing_.find(send_id);
    if (outgoing == outgoing_.end()) {
        return;
    }
    const DeviceId device = outgoing->second.device;
    outgoing_.erase(outgoing);
    transfers_->withdraw(ObjectKind::Sent, send_id);
    observer_.on_send_finished(device, send_id, outcome);
}

void Sends::send_message(const DeviceId& device, protocol::ControlMessage message) {
    if (mesh_ == nullptr) {
        return;
    }
    mesh_->defer([device, message = std::move(message)](mesh::Mesh& mesh, TimePoint now) {
        static_cast<void>(mesh.send_message(device, message, now)); // gone: its session ended
    });
}

// --- mesh::MeshObserver: SendOffer and SendResult stay here, the rest goes on ---

void Sends::on_message(const DeviceId& device, const protocol::ControlMessage& message) {
    if (const auto* offer = std::get_if<protocol::SendOffer>(&message)) {
        receive_offer(device, *offer);
        return;
    }
    if (const auto* result = std::get_if<protocol::SendResult>(&message)) {
        receive_result(device, *result);
        return;
    }
    next_.on_message(device, message);
}

mesh::TrustVerdict Sends::on_trust_query(const crypto::Key& peer_key,
                                         const protocol::Hello& hello) {
    return next_.on_trust_query(peer_key, hello);
}

void Sends::on_pairing_code(mesh::ConnectionId connection, std::uint32_t code,
                            const crypto::Key& peer_key, const protocol::Hello& hello) {
    next_.on_pairing_code(connection, code, peer_key, hello);
}

void Sends::on_paired(const crypto::Key& peer_key, const protocol::Hello& hello) {
    next_.on_paired(peer_key, hello);
}

void Sends::on_pairing_failed(mesh::ConnectionId connection, Error reason) {
    next_.on_pairing_failed(connection, reason);
}

void Sends::on_peer_connected(const DeviceId& device, const crypto::Key& peer_key,
                              const protocol::Hello& hello,
                              const protocol::Capabilities& capabilities) {
    minors_[device] = hello.protocol_minor;
    next_.on_peer_connected(device, peer_key, hello, capabilities);
}

void Sends::on_peer_disconnected(const DeviceId& device, std::optional<Error> reason,
                                 std::optional<protocol::ByeReason> bye) {
    minors_.erase(device);
    std::erase_if(incoming_, [&](const auto& send) { return send.first == device; });
    std::vector<std::uint64_t> ended;
    for (const auto& [send_id, outgoing] : outgoing_) {
        if (outgoing.device == device) {
            ended.push_back(send_id);
        }
    }
    for (const std::uint64_t send_id : ended) {
        finish(send_id, SendOutcome::Failed);
    }
    next_.on_peer_disconnected(device, reason, bye);
}

void Sends::on_peer_capabilities(const DeviceId& device,
                                 const protocol::Capabilities& capabilities) {
    next_.on_peer_capabilities(device, capabilities);
}

void Sends::on_peer_responsive(const DeviceId& device, bool responsive) {
    next_.on_peer_responsive(device, responsive);
}

void Sends::on_motion(const DeviceId& device, const protocol::PointerMotion& motion) {
    next_.on_motion(device, motion);
}

void Sends::on_transfer_opened(const DeviceId& device, std::uint32_t stream_id) {
    next_.on_transfer_opened(device, stream_id);
}

void Sends::on_transfer_message(const DeviceId& device, std::uint32_t stream_id,
                                const protocol::TransferMessage& message) {
    next_.on_transfer_message(device, stream_id, message);
}

void Sends::on_transfer_writable(const DeviceId& device, std::uint32_t stream_id) {
    next_.on_transfer_writable(device, stream_id);
}

void Sends::on_transfer_closed(const DeviceId& device, std::uint32_t stream_id,
                               std::optional<Error> reason) {
    next_.on_transfer_closed(device, stream_id, reason);
}

void Sends::on_stats(const DeviceId& device, const mesh::SessionStats& stats) {
    next_.on_stats(device, stats);
}

void Sends::on_log(mesh::LogLevel level, std::string_view message) {
    next_.on_log(level, message);
}

// --- transfer::TransferObserver: a send is offered once its items are scanned ---

void Sends::on_offer_ready(const transfer::OfferReady& ready) {
    const auto found = ready.item.kind == ObjectKind::Sent ? outgoing_.find(ready.item.object_id)
                                                           : outgoing_.end();
    if (found == outgoing_.end()) {
        transfer_observer_.on_offer_ready(ready); // not a send of this layer's
        return;
    }
    Outgoing& outgoing = found->second;
    if (ready.item.item_index >= outgoing.items.size() || outgoing.scanning == 0) {
        return;
    }
    if (ready.error) {
        outgoing.broken = true; // the send cannot travel as a whole
    } else {
        protocol::OfferedItem& item = outgoing.items[ready.item.item_index];
        item.kind = ready.folder ? protocol::ItemKind::Folder : protocol::ItemKind::File;
        item.byte_count = ready.total_bytes;
        item.name = ready.name;
        if (!protocol::check_type_id(item.content_type)) {
            item.content_type = ready.folder ? kFolderType : kDataType;
        }
    }
    if (--outgoing.scanning > 0) {
        return;
    }
    protocol::SendOffer offer{.send_id = found->first, .items = outgoing.items};
    if (outgoing.broken || !protocol::encode_control(offer)) {
        finish(found->first, SendOutcome::Failed); // an item cannot travel, or the names do not fit
        return;
    }
    send_message(outgoing.device, std::move(offer));
}

void Sends::on_transfer_progress(const transfer::TransferProgress& progress) {
    transfer_observer_.on_transfer_progress(progress);
}

void Sends::on_transfer_finished(const transfer::TransferOutcome& outcome) {
    transfer_observer_.on_transfer_finished(outcome);
}

void Sends::on_content_received(const transfer::ContentOutcome& outcome) {
    transfer_observer_.on_content_received(outcome);
}

void Sends::on_send_progress(const transfer::SendProgress& progress) {
    transfer_observer_.on_send_progress(progress);
}

void Sends::on_transfer_planned(const transfer::TransferPlan& plan) {
    transfer_observer_.on_transfer_planned(plan);
}

void Sends::on_entry_written(const transfer::EntryWritten& written) {
    transfer_observer_.on_entry_written(written);
}

} // namespace osdrop::sending
