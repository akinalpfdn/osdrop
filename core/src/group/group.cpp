#include "group/group.hpp"

#include <algorithm>
#include <utility>
#include <variant>

namespace osdrop::group {

namespace {

template <typename... Visitors>
struct Overloaded : Visitors... { // NOLINT(misc-multiple-inheritance): the standard visitor idiom
    using Visitors::operator()...;
};

bool requests_clipboard(const protocol::TransferMessage& message) {
    if (const auto* request = std::get_if<protocol::ManifestRequest>(&message)) {
        return request->object.kind == protocol::ObjectKind::Clipboard;
    }
    if (const auto* request = std::get_if<protocol::ContentRequest>(&message)) {
        return request->object.kind == protocol::ObjectKind::Clipboard;
    }
    return false;
}

std::uint32_t request_id_of(const protocol::TransferMessage& message) {
    return std::visit(Overloaded{
                          [](const protocol::ManifestRequest& m) { return m.request_id; },
                          [](const protocol::ContentRequest& m) { return m.request_id; },
                          [](const auto& /*other*/) { return std::uint32_t{0}; },
                      },
                      message);
}

} // namespace

Group::Group(GroupConfig config, const crypto::KeyPair& identity, GroupState state,
             crypto::RandomSource& random, GroupObserver& observer, mesh::MeshObserver& next)
    : config_(std::move(config)), public_key_(identity.public_key()),
      self_(crypto::device_id(public_key_)), state_(std::move(state)), random_(random),
      observer_(observer), next_(next) {}

void Group::attach(mesh::Mesh& mesh) {
    mesh_ = &mesh;
}

void Group::log(LogLevel level, std::string_view message) {
    next_.on_log(level, message);
}

void Group::persist() {
    observer_.on_state_changed(state_);
}

Member Group::self_member(std::uint32_t epoch) const {
    return Member{.device_id = self_,
                  .public_key = public_key_,
                  .epoch = epoch,
                  .platform = config_.platform,
                  .name = config_.device_name};
}

std::vector<MemberStatus> Group::members() const {
    std::vector<MemberStatus> list;
    for (const Member& member : state_.roster.members()) {
        list.push_back({.member = member,
                        .permissions = state_.permissions_for(member.device_id),
                        .self = member.device_id == self_,
                        .connected = sessions_.contains(member.device_id)});
    }
    return list;
}

// --- Trust ---------------------------------------------------------------------------------------

TrustVerdict Group::on_trust_query(const crypto::Key& peer_key, const protocol::Hello& hello) {
    const DeviceId device = crypto::device_id(peer_key);
    if (const auto member = state_.roster.member(device)) {
        return {.decision = TrustDecision::Trusted,
                .refusal = Error::NotTrusted,
                .features = offered_features(state_.permissions_for(device))};
    }
    if (state_.roster.pairings().contains(device)) {
        return {.decision = TrustDecision::Blocked, .refusal = Error::NotTrusted};
    }
    const bool peer_in_group = !protocol::is_zero(hello.group_id);
    if (state_.in_group() && peer_in_group) {
        // Another group, or a new member this computer has not heard of yet: the mesh dials it
        // again once a roster from a member names it.
        return {.decision = TrustDecision::Blocked,
                .refusal =
                    hello.group_id == state_.group_id ? Error::NotTrusted : Error::InAnotherGroup};
    }
    if (!pairing_prompts_.empty()) {
        return {.decision = TrustDecision::Blocked, .refusal = Error::PeerBusy}; // one at a time
    }
    if (state_.in_group() && state_.roster.size() >= kMaxGroupSize) {
        return {.decision = TrustDecision::Blocked, .refusal = Error::GroupFull};
    }
    return {.decision = TrustDecision::Unknown,
            .refusal = Error::NotTrusted,
            .features = offered_features(Permissions{})};
}

void Group::on_pairing_code(mesh::ConnectionId connection, std::uint32_t code,
                            const crypto::Key& peer_key, const protocol::Hello& hello) {
    pairing_prompts_[connection] = crypto::device_id(peer_key);
    observer_.on_pairing_code(connection, code, peer_key, hello);
}

void Group::on_paired(const crypto::Key& peer_key, const protocol::Hello& hello) {
    const DeviceId device = crypto::device_id(peer_key);
    std::erase_if(pairing_prompts_, [&](const auto& entry) { return entry.second == device; });
    joining_[device] = Joining{.key = peer_key, .hello = hello};
    log(LogLevel::Info, "Paired with " + hello.device_name);
}

void Group::on_pairing_failed(mesh::ConnectionId connection, Error reason) {
    pairing_prompts_.erase(connection);
    observer_.on_pairing_failed(connection, reason);
}

// --- Sessions ------------------------------------------------------------------------------------

void Group::on_peer_connected(const DeviceId& device, const crypto::Key& peer_key,
                              const protocol::Hello& hello,
                              const protocol::Capabilities& capabilities) {
    sessions_[device] = Session{.key = peer_key, .hello = hello, .capabilities = capabilities};
    if (const auto it = joining_.find(device); it != joining_.end()) {
        const Joining joining = it->second;
        if (state_.in_group()) {
            joining_.erase(it);
            add_member(device, joining);
        } else if (protocol::is_zero(hello.group_id) && self_ < device) {
            joining_.erase(it);
            create_group_with(device, joining);
        }
        // Otherwise the peer adds this computer to its group, or creates the group, and its
        // RosterState follows.
        return;
    }
    if (state_.roster.is_member(device)) {
        send_roster(device); // roster sync on connect
        announce(device);
    }
}

void Group::on_peer_disconnected(const DeviceId& device, std::optional<Error> reason,
                                 std::optional<protocol::ByeReason> bye) {
    sessions_.erase(device);
    joining_.erase(device);
    if (announced_.erase(device) > 0) {
        next_.on_peer_disconnected(device, reason, bye);
    }
    if (bye == protocol::ByeReason::Revoked && state_.roster.is_member(device)) {
        log(LogLevel::Warning, "A member removed this computer from the group");
        clear(Error::NotTrusted, protocol::ByeReason::LeavingGroup);
    }
}

void Group::announce(const DeviceId& device) {
    const auto session = sessions_.find(device);
    if (session == sessions_.end() || !state_.roster.is_member(device) ||
        !announced_.insert(device).second) {
        return;
    }
    next_.on_peer_connected(device, session->second.key, session->second.hello,
                            session->second.capabilities);
}

// --- Roster --------------------------------------------------------------------------------------

void Group::send_roster(const DeviceId& device) {
    if (mesh_ == nullptr || !state_.in_group()) {
        return;
    }
    for (protocol::RosterState& message : state_.roster.to_messages(state_.group_id)) {
        mesh_->defer([device, message = std::move(message)](mesh::Mesh& mesh, TimePoint now) {
            static_cast<void>(mesh.send_message(device, message, now)); // gone peers sync later
        });
    }
}

void Group::add_member(const DeviceId& device, const Joining& joining) {
    state_.roster.add(Member{.device_id = device,
                             .public_key = joining.key,
                             .epoch = state_.roster.next_epoch(device),
                             .platform = joining.hello.platform,
                             .name = joining.hello.device_name});
    log(LogLevel::Info, "Added " + joining.hello.device_name + " to the group");
    roster_changed(std::nullopt);
    announce(device);
}

void Group::create_group_with(const DeviceId& device, const Joining& joining) {
    protocol::Id group{};
    if (!random_.fill(group) || protocol::is_zero(group)) {
        log(LogLevel::Error, "No randomness for a new group ID");
        return;
    }
    state_.group_id = group;
    state_.roster.add(self_member(state_.roster.next_epoch(self_)));
    if (mesh_ != nullptr) {
        mesh_->defer([group](mesh::Mesh& mesh, TimePoint now) { mesh.set_group(group, now); });
    }
    log(LogLevel::Info, "Created a group");
    add_member(device, joining);
}

void Group::receive_roster(const DeviceId& device, const protocol::RosterState& message) {
    if (joining_.contains(device) && !state_.in_group()) {
        adopt(device, message);
        return;
    }
    // Only members speak for the group: its ID is public, so a stranger could claim it.
    if (!state_.in_group() || message.group_id != state_.group_id ||
        !state_.roster.is_member(device)) {
        log(LogLevel::Warning, "Ignored a roster from a computer outside this group");
        return;
    }
    const auto incoming = Roster::from_message(message);
    if (!incoming) {
        log(LogLevel::Warning, "Ignored a roster with a device ID that is not its key's");
        return;
    }
    if (state_.roster.merge(incoming.value())) {
        roster_changed(device);
    }
}

void Group::adopt(const DeviceId& device, const protocol::RosterState& message) {
    const auto incoming = Roster::from_message(message);
    const auto self = incoming ? incoming.value().member(self_) : std::nullopt;
    if (!incoming || !self || self->public_key != public_key_ ||
        !incoming.value().is_member(device) || protocol::is_zero(message.group_id)) {
        log(LogLevel::Warning, "Ignored a roster that does not add this computer");
        return;
    }
    state_.group_id = message.group_id;
    state_.roster = incoming.value();
    joining_.erase(device);
    if (mesh_ != nullptr) {
        mesh_->defer([group = state_.group_id](mesh::Mesh& mesh, TimePoint now) {
            mesh.set_group(group, now);
        });
    }
    log(LogLevel::Info, "Joined a group");
    roster_changed(device);
    for (const auto& [peer, session] : sessions_) {
        announce(peer);
    }
}

void Group::roster_changed(std::optional<DeviceId> from) {
    persist();
    if (state_.in_group() && state_.roster.pairings().contains(self_) &&
        !state_.roster.is_member(self_)) {
        log(LogLevel::Warning, "This computer was removed from the group");
        clear(Error::NotTrusted, protocol::ByeReason::LeavingGroup);
        return;
    }
    if (mesh_ == nullptr) {
        return;
    }
    for (const auto& [peer, session] : sessions_) {
        if (state_.roster.is_member(peer)) {
            if (peer != from) {
                send_roster(peer);
            }
        } else if (state_.roster.pairings().contains(peer)) {
            mesh_->defer([peer](mesh::Mesh& mesh, TimePoint now) {
                mesh.disconnect(peer, protocol::ByeReason::Revoked, now);
            });
        }
    }
    // Peers refused before may be members now.
    mesh_->defer([](mesh::Mesh& mesh, TimePoint now) { mesh.trust_changed(now); });
}

void Group::clear(std::optional<Error> reason, protocol::ByeReason bye) {
    state_ = GroupState{};
    joining_.clear();
    if (mesh_ != nullptr) {
        mesh_->defer([bye](mesh::Mesh& mesh, TimePoint now) {
            mesh.close_all(bye, now);
            mesh.set_group(protocol::Id{}, now);
        });
    }
    persist();
    observer_.on_left_group(reason);
}

// --- Commands ------------------------------------------------------------------------------------

Status<Error> Group::revoke(const DeviceId& device, TimePoint now) {
    if (!state_.in_group()) {
        return Status<Error>::failure(Error::InvalidState);
    }
    const auto member = state_.roster.member(device);
    if (device == self_ || !member) {
        return Status<Error>::failure(Error::InvalidArgument);
    }
    state_.roster.revoke(device, member->epoch);
    state_.permissions.erase(device);
    log(LogLevel::Info, "Revoked " + member->name);
    roster_changed(std::nullopt); // tells the others and disconnects it with Bye 6
    if (mesh_ != nullptr) {
        mesh_->flush(now);
    }
    return Status<Error>::ok();
}

Status<Error> Group::leave(TimePoint now) {
    if (!state_.in_group()) {
        return Status<Error>::failure(Error::InvalidState);
    }
    if (const auto self = state_.roster.member(self_)) {
        state_.roster.revoke(self_, self->epoch);
    }
    // Members learn of it before the sessions end; the others from them later.
    for (const auto& [peer, session] : sessions_) {
        if (state_.roster.is_member(peer)) {
            send_roster(peer);
        }
    }
    log(LogLevel::Info, "Left the group");
    clear(std::nullopt, protocol::ByeReason::LeavingGroup);
    if (mesh_ != nullptr) {
        mesh_->flush(now);
    }
    return Status<Error>::ok();
}

Status<Error> Group::set_permissions(const DeviceId& device, const Permissions& permissions,
                                     TimePoint now) {
    if (!state_.in_group()) {
        return Status<Error>::failure(Error::InvalidState);
    }
    if (device == self_ || !state_.roster.is_member(device)) {
        return Status<Error>::failure(Error::InvalidArgument);
    }
    if (permissions == Permissions{}) {
        state_.permissions.erase(device);
    } else {
        state_.permissions[device] = permissions;
    }
    persist();
    if (mesh_ != nullptr) {
        mesh_->update_features(device, offered_features(permissions), now);
    }
    return Status<Error>::ok();
}

// --- Messages ------------------------------------------------------------------------------------

bool Group::admits(const DeviceId& device, const protocol::ControlMessage& message) {
    if (!state_.roster.is_member(device)) {
        return false;
    }
    const Permissions permissions = state_.permissions_for(device);
    const auto refuse = [&](std::uint32_t control_id) {
        if (mesh_ != nullptr) {
            mesh_->defer([device, control_id](mesh::Mesh& mesh, TimePoint now) {
                static_cast<void>(mesh.send_message(
                    device,
                    protocol::Refuse{.control_id = control_id,
                                     .reason = protocol::RefuseReason::NotPermitted},
                    now));
            });
        }
    };
    return std::visit(
        Overloaded{
            [&](const protocol::Enter& m) {
                // A drag carried across brings files with it.
                const bool allowed = permissions.control && (m.drag_id == 0 || permissions.files);
                if (!allowed) {
                    refuse(m.control_id);
                }
                return allowed;
            },
            [&](const protocol::KeyEvent& /*m*/) { return permissions.control; },
            [&](const protocol::ButtonEvent& /*m*/) { return permissions.control; },
            [&](const protocol::ScrollEvent& /*m*/) { return permissions.control; },
            [&](const protocol::ReleaseAll& /*m*/) { return permissions.control; },
            [&](const protocol::DragOffer& m) {
                // A forwarded offer brings files from its source, which may be a third member.
                const bool source_sends =
                    m.source == self_ ||
                    (state_.roster.is_member(m.source) && state_.permissions_for(m.source).files);
                return m.probe_id != 0 || (permissions.files && source_sends);
            },
            [&](const protocol::ClipboardAnnounce& /*m*/) { return permissions.clipboard; },
            [&](const auto& /*other*/) { return true; },
        },
        message);
}

void Group::on_message(const DeviceId& device, const protocol::ControlMessage& message) {
    if (const auto* roster = std::get_if<protocol::RosterState>(&message)) {
        receive_roster(device, *roster);
        return;
    }
    if (admits(device, message)) {
        next_.on_message(device, message);
    }
}

void Group::on_motion(const DeviceId& device, const protocol::PointerMotion& motion) {
    if (state_.roster.is_member(device) && state_.permissions_for(device).control) {
        next_.on_motion(device, motion);
    }
}

void Group::on_transfer_message(const DeviceId& device, std::uint32_t stream_id,
                                const protocol::TransferMessage& message) {
    if (!state_.roster.is_member(device)) {
        return;
    }
    if (requests_clipboard(message) && !state_.permissions_for(device).clipboard) {
        if (mesh_ != nullptr) {
            const std::uint32_t request = request_id_of(message);
            mesh_->defer([device, stream_id, request](mesh::Mesh& mesh, TimePoint now) {
                static_cast<void>(
                    mesh.send_transfer(device, stream_id,
                                       protocol::TransferError{
                                           .request_id = request,
                                           .code = static_cast<std::uint16_t>(Error::NotPermitted)},
                                       now));
            });
        }
        return;
    }
    next_.on_transfer_message(device, stream_id, message);
}

// --- Passed on for members -----------------------------------------------------------------------

void Group::on_peer_capabilities(const DeviceId& device,
                                 const protocol::Capabilities& capabilities) {
    if (const auto it = sessions_.find(device); it != sessions_.end()) {
        it->second.capabilities = capabilities;
    }
    if (announced_.contains(device)) {
        next_.on_peer_capabilities(device, capabilities);
    }
}

void Group::on_peer_responsive(const DeviceId& device, bool responsive) {
    if (announced_.contains(device)) {
        next_.on_peer_responsive(device, responsive);
    }
}

void Group::on_transfer_opened(const DeviceId& device, std::uint32_t stream_id) {
    if (state_.roster.is_member(device)) {
        next_.on_transfer_opened(device, stream_id);
    }
}

void Group::on_transfer_writable(const DeviceId& device, std::uint32_t stream_id) {
    if (state_.roster.is_member(device)) {
        next_.on_transfer_writable(device, stream_id);
    }
}

void Group::on_transfer_closed(const DeviceId& device, std::uint32_t stream_id,
                               std::optional<Error> reason) {
    next_.on_transfer_closed(device, stream_id, reason);
}

void Group::on_stats(const DeviceId& device, const SessionStats& stats) {
    if (announced_.contains(device)) {
        next_.on_stats(device, stats);
    }
}

void Group::on_log(LogLevel level, std::string_view message) {
    next_.on_log(level, message);
}

} // namespace osdrop::group
