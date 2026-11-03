#include "layout/layout.hpp"

#include <utility>
#include <variant>

namespace osdrop::layout {

Layout::Layout(const DeviceId& self, LayoutState state, LayoutObserver& observer,
               mesh::MeshObserver& next)
    : self_(self), state_(std::move(state)), observer_(observer), next_(next), members_{self} {}

void Layout::attach(mesh::Mesh& mesh) {
    mesh_ = &mesh;
}

Arrangement Layout::arrangement(const std::set<DeviceId>& reachable) const {
    return Arrangement::build(state_.document.placements, state_.screens, reachable);
}

void Layout::persist() {
    observer_.on_layout_changed(state_);
}

void Layout::send(const DeviceId& device, protocol::ControlMessage message) {
    if (mesh_ == nullptr) {
        return;
    }
    mesh_->defer([device, message = std::move(message)](mesh::Mesh& mesh, TimePoint now) {
        static_cast<void>(mesh.send_message(device, message, now)); // gone peers sync later
    });
}

void Layout::send_document(std::optional<DeviceId> except) {
    if (state_.document.placements.empty()) {
        return;
    }
    const protocol::LayoutState message = state_.document.to_message();
    for (const DeviceId& device : connected_) {
        if (device != except) {
            send(device, message);
        }
    }
}

void Layout::publish(Placements placements) {
    state_.document = LayoutDocument{.revision = state_.document.revision + 1,
                                     .author = self_,
                                     .placements = std::move(placements)};
    persist();
    send_document(std::nullopt);
}

void Layout::place_self() {
    const auto own = state_.screens.find(self_);
    if (own == state_.screens.end() || own->second.empty() ||
        state_.document.placements.contains(self_)) {
        return;
    }
    Placements placements;
    for (const auto& [device, placement] : state_.document.placements) {
        if (members_.contains(device)) {
            placements.emplace(device, placement);
        }
    }
    placements[self_] = {.offset = place_beside(placements, state_.screens, own->second),
                         .detached = false};
    publish(std::move(placements));
}

// --- Commands ------------------------------------------------------------------------------------

void Layout::group_changed(const protocol::Id& group, std::set<DeviceId> members) {
    members.insert(self_);
    members_ = std::move(members);
    bool changed = false;
    if (group != state_.group_id) {
        // Another group's arrangement means nothing here; only this computer's screens remain.
        const auto own = state_.screens.find(self_);
        ScreenMap screens;
        if (own != state_.screens.end()) {
            screens.emplace(self_, own->second);
        }
        state_ = LayoutState{.group_id = group, .document = {}, .screens = std::move(screens)};
        changed = true;
    }
    changed =
        std::erase_if(state_.screens,
                      [&](const auto& entry) { return !members_.contains(entry.first); }) > 0 ||
        changed;
    if (changed) {
        persist();
    }
    place_self();
}

Status<Error> Layout::set_screens(Screens screens, TimePoint now) {
    if (screens.empty()) {
        return Status<Error>::failure(Error::InvalidArgument);
    }
    const protocol::ScreenReport report = screens.to_report();
    if (const auto valid = protocol::encode_control(report); !valid) {
        return Status<Error>::failure(valid.error());
    }
    Screens& own = state_.screens[self_];
    if (own != screens) {
        own = std::move(screens);
        persist();
    }
    reported_ = true;
    for (const DeviceId& device : connected_) {
        send(device, report);
    }
    place_self();
    if (mesh_ != nullptr) {
        mesh_->flush(now);
    }
    return Status<Error>::ok();
}

Status<Error> Layout::set_placements(Placements placements, TimePoint now) {
    for (const auto& [device, placement] : placements) {
        if (!members_.contains(device)) {
            return Status<Error>::failure(Error::InvalidArgument);
        }
    }
    const LayoutDocument proposal{
        .revision = state_.document.revision + 1, .author = self_, .placements = placements};
    if (const auto valid = protocol::encode_control(proposal.to_message()); !valid) {
        return Status<Error>::failure(valid.error());
    }
    if (!check(placements, state_.screens).empty()) {
        return Status<Error>::failure(Error::InvalidLayout);
    }
    publish(std::move(placements));
    if (mesh_ != nullptr) {
        mesh_->flush(now);
    }
    return Status<Error>::ok();
}

// --- Messages ------------------------------------------------------------------------------------

void Layout::receive_screens(const DeviceId& device, const protocol::ScreenReport& report) {
    Screens screens = Screens::from_report(report);
    Screens& known = state_.screens[device];
    if (known != screens) {
        known = std::move(screens);
        persist();
    }
}

void Layout::receive_document(const DeviceId& device, const protocol::LayoutState& message) {
    if (state_.document.merge(LayoutDocument::from_message(message))) {
        persist();
        send_document(device); // members pass it on until everyone has it
        place_self();
    }
}

void Layout::on_message(const DeviceId& device, const protocol::ControlMessage& message) {
    if (const auto* report = std::get_if<protocol::ScreenReport>(&message)) {
        receive_screens(device, *report);
        return;
    }
    if (const auto* document = std::get_if<protocol::LayoutState>(&message)) {
        receive_document(device, *document);
        return;
    }
    next_.on_message(device, message);
}

void Layout::on_peer_connected(const DeviceId& device, const crypto::Key& peer_key,
                               const protocol::Hello& hello,
                               const protocol::Capabilities& capabilities) {
    connected_.insert(device);
    const auto own = state_.screens.find(self_);
    if (reported_ && own != state_.screens.end()) {
        send(device, own->second.to_report());
    }
    if (!state_.document.placements.empty()) {
        send(device, state_.document.to_message());
    }
    next_.on_peer_connected(device, peer_key, hello, capabilities);
}

void Layout::on_peer_disconnected(const DeviceId& device, std::optional<Error> reason,
                                  std::optional<protocol::ByeReason> bye) {
    connected_.erase(device);
    next_.on_peer_disconnected(device, reason, bye);
}

// --- Passed on -----------------------------------------------------------------------------------

mesh::TrustVerdict Layout::on_trust_query(const crypto::Key& peer_key,
                                          const protocol::Hello& hello) {
    return next_.on_trust_query(peer_key, hello);
}

void Layout::on_pairing_code(mesh::ConnectionId connection, std::uint32_t code,
                             const crypto::Key& peer_key, const protocol::Hello& hello) {
    next_.on_pairing_code(connection, code, peer_key, hello);
}

void Layout::on_paired(const crypto::Key& peer_key, const protocol::Hello& hello) {
    next_.on_paired(peer_key, hello);
}

void Layout::on_pairing_failed(mesh::ConnectionId connection, Error reason) {
    next_.on_pairing_failed(connection, reason);
}

void Layout::on_peer_capabilities(const DeviceId& device,
                                  const protocol::Capabilities& capabilities) {
    next_.on_peer_capabilities(device, capabilities);
}

void Layout::on_peer_responsive(const DeviceId& device, bool responsive) {
    next_.on_peer_responsive(device, responsive);
}

void Layout::on_motion(const DeviceId& device, const protocol::PointerMotion& motion) {
    next_.on_motion(device, motion);
}

void Layout::on_transfer_opened(const DeviceId& device, std::uint32_t stream_id) {
    next_.on_transfer_opened(device, stream_id);
}

void Layout::on_transfer_message(const DeviceId& device, std::uint32_t stream_id,
                                 const protocol::TransferMessage& message) {
    next_.on_transfer_message(device, stream_id, message);
}

void Layout::on_transfer_writable(const DeviceId& device, std::uint32_t stream_id) {
    next_.on_transfer_writable(device, stream_id);
}

void Layout::on_transfer_closed(const DeviceId& device, std::uint32_t stream_id,
                                std::optional<Error> reason) {
    next_.on_transfer_closed(device, stream_id, reason);
}

void Layout::on_stats(const DeviceId& device, const mesh::SessionStats& stats) {
    next_.on_stats(device, stats);
}

void Layout::on_log(mesh::LogLevel level, std::string_view message) {
    next_.on_log(level, message);
}

} // namespace osdrop::layout
