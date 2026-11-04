#include "control/control.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ranges>
#include <utility>

namespace osdrop::control {

namespace {

template <typename... Visitors>
struct Overloaded : Visitors... { // NOLINT(misc-multiple-inheritance): the standard visitor idiom
    using Visitors::operator()...;
};

/// The session clock in microseconds, as Ping and PointerMotion carry it.
std::int64_t micros(TimePoint time) {
    return std::chrono::duration_cast<Microseconds>(time.time_since_epoch()).count();
}

std::int32_t coord(std::int64_t value) {
    return static_cast<std::int32_t>(value); // positions stay within kMaxCoord
}

Point point(std::int32_t x, std::int32_t y) {
    return {.x = x, .y = y};
}

constexpr std::size_t kPercent = 100;
constexpr std::size_t kMedianPercent = 50;
constexpr std::size_t kP99Percent = 99;

} // namespace

Control::Control(const DeviceId& self, const ControlTunables& tunables, ControlObserver& observer,
                 layout::LayoutObserver& layout_observer, mesh::MeshObserver& next)
    : self_(self), tunables_(tunables), observer_(observer), layout_observer_(layout_observer),
      next_(next), engine_(self, tunables.push) {}

void Control::attach(mesh::Mesh& mesh, const layout::Layout& layout) {
    mesh_ = &mesh;
    layout_ = &layout;
    refresh();
}

std::optional<DeviceId> Control::controller() const {
    return controlled_ ? std::optional(controlled_->controller) : std::nullopt;
}

// --- Commands --------------------------------------------------------------------------------

void Control::set_controllers(std::set<DeviceId> controllers) {
    controllers_ = std::move(controllers);
    if (controlled_ && !controllers_.contains(controlled_->controller)) {
        send(controlled_->controller,
             protocol::Refuse{.control_id = controlled_->control_id,
                              .reason = protocol::RefuseReason::NotPermitted});
        release();
    }
}

void Control::input(const Input& input, TimePoint captured) {
    if (controlled_) {
        take_back(); // local input always wins
    }
    std::visit(
        Overloaded{
            [&](const PointerMoved& m) { moved(m, captured); },
            [&](const KeyInput& m) {
                physical_.key(m.key, m.action);
                if (away_) {
                    send(away_->device, protocol::KeyEvent{.control_id = away_->control_id,
                                                           .page = m.key.page,
                                                           .usage = m.key.usage,
                                                           .action = m.action});
                }
            },
            [&](const ButtonInput& m) {
                physical_.button(m.button, m.action);
                update_lock(); // no crossing with a button held
                if (away_) {
                    const Point at = engine_.position();
                    send(away_->device, protocol::ButtonEvent{.control_id = away_->control_id,
                                                              .button = m.button,
                                                              .action = m.action,
                                                              .click_count = m.click_count,
                                                              .x = coord(at.x),
                                                              .y = coord(at.y)});
                }
            },
            [&](const ScrollInput& m) {
                if (away_) {
                    const Point at = engine_.position();
                    send(away_->device, protocol::ScrollEvent{.control_id = away_->control_id,
                                                              .unit = m.unit,
                                                              .phase = m.phase,
                                                              .momentum = m.momentum,
                                                              .dx = m.dx,
                                                              .dy = m.dy,
                                                              .x = coord(at.x),
                                                              .y = coord(at.y)});
                }
            },
        },
        input);
}

void Control::return_home(protocol::LeaveReason reason) {
    if (!away_) {
        return;
    }
    send(away_->device, protocol::Leave{.control_id = away_->control_id, .reason = reason});
    come_home(engine_.return_home().position,
              reason == protocol::LeaveReason::ReleasedByHotkey ? CaptureEnd::Hotkey
                                                                : CaptureEnd::InputLost,
              std::nullopt);
}

void Control::set_locked(bool locked) {
    locked_ = locked;
    update_lock();
}

void Control::set_unavailable(std::optional<protocol::RefuseReason> reason) {
    unavailable_ = reason;
    if (reason && controlled_) {
        send(controlled_->controller,
             protocol::Refuse{.control_id = controlled_->control_id, .reason = *reason});
        release();
    }
}

void Control::cursor_moved(Point position) {
    if (!controlled_) {
        return;
    }
    const Point last = controlled_->position;
    if (std::max(std::abs(position.x - last.x), std::abs(position.y - last.y)) <=
        tunables_.cursor_report_distance) {
        return;
    }
    controlled_->position = position;
    send(controlled_->controller, protocol::CursorReport{.control_id = controlled_->control_id,
                                                         .x = coord(position.x),
                                                         .y = coord(position.y)});
}

// --- Controller --------------------------------------------------------------------------------

void Control::moved(const PointerMoved& moved, TimePoint captured) {
    const layout::CursorUpdate update = engine_.motion(captured, moved.position, moved.delta);
    if (update.device == self_) {
        if (away_) { // pushed back through an edge
            send(away_->device, protocol::Leave{.control_id = away_->control_id,
                                                .reason = protocol::LeaveReason::Returned});
            come_home(update.position, CaptureEnd::Returned, std::nullopt);
        }
        return;
    }
    if (update.crossed) {
        cross(update.device, update.position);
        return;
    }
    if (away_ && update.position != away_->sent) {
        away_->sent = update.position;
        const protocol::PointerMotion motion{.control_id = away_->control_id,
                                             .x = coord(update.position.x),
                                             .y = coord(update.position.y),
                                             .capture_time_us =
                                                 static_cast<std::uint64_t>(micros(captured))};
        mesh_->defer([device = away_->device, motion](mesh::Mesh& mesh, TimePoint now) {
            static_cast<void>(mesh.send_motion(device, motion, now)); // lost peers: refresh()
        });
    }
}

void Control::cross(const DeviceId& device, Point position) {
    if (away_) {
        send(away_->device, protocol::Leave{.control_id = away_->control_id,
                                            .reason = protocol::LeaveReason::CrossedOn});
    } else {
        observer_.on_capture_began(device, physical_.keys());
    }
    away_ = Away{.device = device, .control_id = next_control_id(), .sent = position};
    send(device, protocol::Enter{.control_id = away_->control_id,
                                 .x = coord(position.x),
                                 .y = coord(position.y),
                                 .modifiers = physical_.modifiers(),
                                 .drag_id = 0});
}

void Control::come_home(Point cursor, CaptureEnd reason,
                        std::optional<protocol::RefuseReason> refused) {
    if (!away_) {
        return;
    }
    const DeviceId device = away_->device;
    away_.reset();
    observer_.on_capture_ended(
        {.device = device, .cursor = cursor, .reason = reason, .refused = refused});
}

void Control::refresh() {
    if (layout_ == nullptr) {
        return;
    }
    std::set<DeviceId> reachable{self_};
    for (const auto& [device, peer] : peers_) {
        if (peer.responsive && peer.accepts_control) {
            reachable.insert(device);
        }
    }
    const auto update = engine_.set_arrangement(layout_->arrangement(reachable));
    if (update && away_) {
        // Told in case it still hears: it releases what it holds.
        send(away_->device, protocol::Leave{.control_id = away_->control_id,
                                            .reason = protocol::LeaveReason::Returned});
        come_home(update->position, CaptureEnd::PeerLost, std::nullopt);
    }
}

void Control::update_lock() {
    engine_.set_locked(locked_ || !physical_.buttons().empty());
}

void Control::receive_refusal(const DeviceId& device, std::uint32_t control_id, CaptureEnd reason,
                              std::optional<protocol::RefuseReason> refused) {
    if (away_ && away_->device == device && away_->control_id == control_id) {
        come_home(engine_.return_home().position, reason, refused);
    }
}

std::uint32_t Control::next_control_id() {
    last_control_id_ = last_control_id_ == UINT32_MAX ? 1 : last_control_id_ + 1;
    return last_control_id_;
}

// --- Endpoint ----------------------------------------------------------------------------------

void Control::receive_enter(const DeviceId& device, const protocol::Enter& enter) {
    const auto refuse = [&](protocol::RefuseReason reason) {
        send(device, protocol::Refuse{.control_id = enter.control_id, .reason = reason});
    };
    if (unavailable_) {
        refuse(*unavailable_);
        return;
    }
    if (!controllers_.contains(device)) {
        refuse(protocol::RefuseReason::NotPermitted);
        return;
    }
    // This computer's own input is in use elsewhere, or another computer controls it.
    if (away_ || (controlled_ && controlled_->controller != device)) {
        refuse(protocol::RefuseReason::Busy);
        return;
    }
    if (controlled_) {
        release(); // the controller's previous entry ended with this one
    }
    Controlled& controlled = controlled_.emplace(Controlled{.controller = device,
                                                            .control_id = enter.control_id,
                                                            .held = {},
                                                            .position = point(enter.x, enter.y)});
    observer_.on_controlled(device);
    observer_.on_inject(MoveCursor{.position = controlled.position});
    for (const HeldKey& key : modifier_keys(enter.modifiers)) {
        inject_key(controlled, {.control_id = enter.control_id,
                                .page = key.page,
                                .usage = key.usage,
                                .action = protocol::KeyAction::Down});
    }
}

Control::Controlled* Control::controlled_by(const DeviceId& device, std::uint32_t control_id) {
    if (controlled_ && controlled_->controller == device && controlled_->control_id == control_id) {
        return &*controlled_;
    }
    return nullptr;
}

void Control::inject_key(Controlled& controlled, const protocol::KeyEvent& event) {
    if (controlled.held.key({.page = event.page, .usage = event.usage}, event.action)) {
        observer_.on_inject(event); // a release of a key never pressed here is dropped
    }
}

void Control::inject_button(Controlled& controlled, const protocol::ButtonEvent& event) {
    controlled.position = point(event.x, event.y);
    if (controlled.held.button(event.button, event.action)) {
        observer_.on_inject(event);
    }
}

void Control::receive_motion(Controlled& controlled, const protocol::PointerMotion& motion) {
    controlled.position = point(motion.x, motion.y);
    observer_.on_inject(MoveCursor{.position = controlled.position});
    const auto offset = clock_offsets_.find(controlled.controller);
    if (offset == clock_offsets_.end() || tunables_.latency_samples == 0) {
        return;
    }
    // The controller's capture time on this computer's clock.
    const std::int64_t captured =
        static_cast<std::int64_t>(motion.capture_time_us) - offset->second;
    mesh_->defer([this, captured](mesh::Mesh& /*mesh*/, TimePoint now) {
        const std::int64_t latency = std::max<std::int64_t>(micros(now) - captured, 0);
        if (latencies_.size() < tunables_.latency_samples) {
            latencies_.push_back(latency);
        } else {
            latencies_.at(next_latency_) = latency;
            next_latency_ = (next_latency_ + 1) % latencies_.size();
        }
        latency_changed_ = true;
    });
}

void Control::take_back() {
    if (!controlled_) {
        return;
    }
    send(controlled_->controller, protocol::LocalTakeover{.control_id = controlled_->control_id});
    release();
}

void Control::release() {
    if (!controlled_) {
        return;
    }
    release_held(*controlled_);
    controlled_.reset();
    latencies_.clear();
    next_latency_ = 0;
    latency_changed_ = false;
    observer_.on_controlled(std::nullopt);
}

void Control::release_held(Controlled& controlled) {
    for (const HeldKey& key : std::views::reverse(controlled.held.keys())) {
        observer_.on_inject(protocol::KeyEvent{.control_id = controlled.control_id,
                                               .page = key.page,
                                               .usage = key.usage,
                                               .action = protocol::KeyAction::Up});
    }
    for (const std::uint8_t button : std::views::reverse(controlled.held.buttons())) {
        observer_.on_inject(protocol::ButtonEvent{.control_id = controlled.control_id,
                                                  .button = button,
                                                  .action = protocol::ButtonAction::Up,
                                                  .click_count = 1,
                                                  .x = coord(controlled.position.x),
                                                  .y = coord(controlled.position.y)});
    }
    controlled.held.clear();
}

void Control::report_latency(const DeviceId& device) {
    if (!controlled_ || controlled_->controller != device || !latency_changed_ ||
        latencies_.empty()) {
        return;
    }
    latency_changed_ = false;
    std::vector<std::int64_t> sorted = latencies_;
    std::ranges::sort(sorted);
    const auto percentile = [&](std::size_t percent) {
        return Microseconds{sorted.at((sorted.size() - 1) * percent / kPercent)};
    };
    observer_.on_input_latency(device, {.samples = sorted.size(),
                                        .median = percentile(kMedianPercent),
                                        .p99 = percentile(kP99Percent)});
}

// --- Messages ----------------------------------------------------------------------------------

bool Control::handle(const DeviceId& device, const protocol::ControlMessage& message) {
    return std::visit(
        Overloaded{
            [&](const protocol::Enter& m) {
                receive_enter(device, m);
                return true;
            },
            [&](const protocol::Leave& m) {
                if (controlled_by(device, m.control_id) != nullptr) {
                    release();
                }
                return true;
            },
            [&](const protocol::Refuse& m) {
                receive_refusal(device, m.control_id, CaptureEnd::Refused, m.reason);
                return true;
            },
            [&](const protocol::LocalTakeover& m) {
                receive_refusal(device, m.control_id, CaptureEnd::Takeover, std::nullopt);
                return true;
            },
            [&](const protocol::KeyEvent& m) {
                if (Controlled* controlled = controlled_by(device, m.control_id)) {
                    inject_key(*controlled, m);
                }
                return true;
            },
            [&](const protocol::ButtonEvent& m) {
                if (Controlled* controlled = controlled_by(device, m.control_id)) {
                    inject_button(*controlled, m);
                }
                return true;
            },
            [&](const protocol::ScrollEvent& m) {
                if (Controlled* controlled = controlled_by(device, m.control_id)) {
                    controlled->position = point(m.x, m.y);
                    observer_.on_inject(m);
                }
                return true;
            },
            [&](const protocol::ReleaseAll& m) {
                if (Controlled* controlled = controlled_by(device, m.control_id)) {
                    release_held(*controlled); // the control goes on
                }
                return true;
            },
            [&](const protocol::CursorReport& m) {
                if (away_ && away_->device == device && away_->control_id == m.control_id) {
                    engine_.correct(point(m.x, m.y));
                    away_->sent = engine_.position();
                }
                return true;
            },
            [&](const auto& /*other*/) { return false; },
        },
        message);
}

void Control::send(const DeviceId& device, protocol::ControlMessage message) {
    mesh_->defer([device, message = std::move(message)](mesh::Mesh& mesh, TimePoint now) {
        static_cast<void>(mesh.send_message(device, message, now)); // gone peers: refresh()
    });
}

// --- Observers ---------------------------------------------------------------------------------

void Control::on_layout_changed(const layout::LayoutState& state) {
    layout_observer_.on_layout_changed(state);
    refresh();
}

void Control::on_message(const DeviceId& device, const protocol::ControlMessage& message) {
    if (!handle(device, message)) {
        next_.on_message(device, message);
    }
}

void Control::on_motion(const DeviceId& device, const protocol::PointerMotion& motion) {
    if (Controlled* controlled = controlled_by(device, motion.control_id)) {
        receive_motion(*controlled, motion);
    }
}

void Control::on_peer_connected(const DeviceId& device, const crypto::Key& peer_key,
                                const protocol::Hello& hello,
                                const protocol::Capabilities& capabilities) {
    peers_[device] =
        Peer{.responsive = true,
             .accepts_control = (capabilities.features & protocol::kFeatureTakesControl) != 0};
    refresh();
    next_.on_peer_connected(device, peer_key, hello, capabilities);
}

void Control::on_peer_disconnected(const DeviceId& device, std::optional<Error> reason,
                                   std::optional<protocol::ByeReason> bye) {
    peers_.erase(device);
    clock_offsets_.erase(device);
    refresh();
    if (controlled_ && controlled_->controller == device) {
        release();
    }
    next_.on_peer_disconnected(device, reason, bye);
}

void Control::on_peer_capabilities(const DeviceId& device,
                                   const protocol::Capabilities& capabilities) {
    if (auto peer = peers_.find(device); peer != peers_.end()) {
        peer->second.accepts_control =
            (capabilities.features & protocol::kFeatureTakesControl) != 0;
        refresh();
    }
    next_.on_peer_capabilities(device, capabilities);
}

void Control::on_peer_responsive(const DeviceId& device, bool responsive) {
    if (auto peer = peers_.find(device); peer != peers_.end()) {
        peer->second.responsive = responsive;
        refresh();
    }
    if (!responsive && controlled_ && controlled_->controller == device) {
        release();
    }
    next_.on_peer_responsive(device, responsive);
}

void Control::on_stats(const DeviceId& device, const mesh::SessionStats& stats) {
    if (stats.rtt_us != 0) { // the offset is measured once a ping came back
        clock_offsets_[device] = stats.clock_offset_us;
    }
    report_latency(device);
    next_.on_stats(device, stats);
}

mesh::TrustVerdict Control::on_trust_query(const crypto::Key& peer_key,
                                           const protocol::Hello& hello) {
    return next_.on_trust_query(peer_key, hello);
}

void Control::on_pairing_code(mesh::ConnectionId connection, std::uint32_t code,
                              const crypto::Key& peer_key, const protocol::Hello& hello) {
    next_.on_pairing_code(connection, code, peer_key, hello);
}

void Control::on_paired(const crypto::Key& peer_key, const protocol::Hello& hello) {
    next_.on_paired(peer_key, hello);
}

void Control::on_pairing_failed(mesh::ConnectionId connection, Error reason) {
    next_.on_pairing_failed(connection, reason);
}

void Control::on_transfer_opened(const DeviceId& device, std::uint32_t stream_id) {
    next_.on_transfer_opened(device, stream_id);
}

void Control::on_transfer_message(const DeviceId& device, std::uint32_t stream_id,
                                  const protocol::TransferMessage& message) {
    next_.on_transfer_message(device, stream_id, message);
}

void Control::on_transfer_writable(const DeviceId& device, std::uint32_t stream_id) {
    next_.on_transfer_writable(device, stream_id);
}

void Control::on_transfer_closed(const DeviceId& device, std::uint32_t stream_id,
                                 std::optional<Error> reason) {
    next_.on_transfer_closed(device, stream_id, reason);
}

void Control::on_log(mesh::LogLevel level, std::string_view message) {
    next_.on_log(level, message);
}

} // namespace osdrop::control
