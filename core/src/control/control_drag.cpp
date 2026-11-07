// Drags across computers (docs/PROTOCOL.md, "Drag messages"): the parts of Control that probe and
// carry a drag as the controller, offer this computer's own drags as their source, and show the
// drags that arrive here as their target.

#include "control/control.hpp"
#include "protocol/wire.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace osdrop::control {

namespace {

std::int32_t coord(std::int64_t value) {
    return static_cast<std::int32_t>(value); // positions stay within kMaxCoord
}

constexpr std::string_view kFolderType = "public.folder";
constexpr std::string_view kDataType = "public.data";

} // namespace

// --- As the controller ---------------------------------------------------------------------------

bool Control::only_left_held() const {
    const std::vector<std::uint8_t>& buttons = physical_.buttons();
    return buttons.size() == 1 && buttons.front() == kDragButton;
}

void Control::probe(const DeviceId& walled, Point position, TimePoint now) {
    const std::uint8_t required = settings_.crossing_modifiers;
    const bool missing_modifier = required != 0 && (physical_.modifiers() & required) == 0;
    if (carried_ || locked_ || missing_modifier || !only_left_held()) {
        return; // a wall for another reason than the button
    }
    if (walled != self_) {
        const auto peer = peers_.find(walled);
        if (peer == peers_.end() || !peer->second.drag_target) {
            return; // it takes no drags
        }
    }
    const DeviceId under = engine_.device();
    if (under == self_) {
        if (outgoing_ && outgoing_->scanning == 0 && !outgoing_->broken && !outgoing_->outcome) {
            outgoing_->offered_to = self_;
            carry(Carried{.offer = outgoing_offer(*outgoing_, 0)});
        }
        return; // or still measured: the next movement asks again
    }
    const auto peer = peers_.find(under);
    if (peer == peers_.end() || !peer->second.drag_source) {
        return;
    }
    if (probe_ && now - probe_->sent < tunables_.probe_timeout) {
        return; // one at a time
    }
    if (last_probe_ && now - *last_probe_ < tunables_.probe_interval) {
        return;
    }
    last_probe_id_ = last_probe_id_ == UINT32_MAX ? 1 : last_probe_id_ + 1;
    probe_ = Probe{.device = under, .probe_id = last_probe_id_, .sent = now};
    last_probe_ = now;
    send(under, protocol::DragProbe{
                    .probe_id = last_probe_id_, .x = coord(position.x), .y = coord(position.y)});
}

void Control::receive_offer(const DeviceId& device, const protocol::DragOffer& offer) {
    if (!probe_ || probe_->device != device || probe_->probe_id != offer.probe_id) {
        return; // an answer nobody waits for any more
    }
    probe_.reset();
    if (offer.drag_id == 0 || !away_ || away_->device != device || !only_left_held()) {
        return; // no drag there, or the cursor or the button moved on meanwhile
    }
    protocol::DragOffer forwarded = offer;
    forwarded.probe_id = 0;
    carry(Carried{.offer = std::move(forwarded)});
}

void Control::carry(std::optional<Carried> carried) {
    if (!carried && !carried_) {
        return;
    }
    carried_ = std::move(carried);
    refresh();
    update_lock();
}

void Control::carry_home(protocol::DragOffer offer, Point cursor, CaptureEnd reason) {
    if (offer.source == self_) {
        if (outgoing_ && outgoing_->drag_id == offer.drag_id) {
            outgoing_->parked = false; // this computer's own drag goes on here
        }
        return;
    }
    if (reason == CaptureEnd::Returned && only_left_held() &&
        file_senders_.contains(offer.source)) {
        incoming_ = Incoming{.controller = self_, .source = offer.source, .drag_id = offer.drag_id};
        observer_.on_drag_arrived({.source = offer.source,
                                   .drag_id = offer.drag_id,
                                   .items = std::move(offer.items),
                                   .position = cursor});
        return;
    }
    send(offer.source, protocol::DragResult{.drag_id = offer.drag_id,
                                            .outcome = protocol::DragOutcome::Cancelled});
    carry(std::nullopt);
}

// --- As the source -------------------------------------------------------------------------------

Status<Error> Control::drag_started(std::vector<DraggedItem> items) {
    if (transfers_ == nullptr) {
        return Status<Error>::failure(Error::InvalidState);
    }
    if (items.size() > protocol::kMaxDragItems ||
        !std::ranges::all_of(items,
                             [](const DraggedItem& item) { return item.path.is_absolute(); })) {
        return Status<Error>::failure(Error::InvalidArgument);
    }
    withdraw_outgoing(); // a new drag replaces the one before
    if (items.empty()) {
        return Status<Error>::ok();
    }
    last_drag_id_ = last_drag_id_ == UINT32_MAX ? 1 : last_drag_id_ + 1;
    std::set<DeviceId> audience;
    for (const auto& [device, peer] : peers_) {
        audience.insert(device);
    }
    outgoing_ = Outgoing{.drag_id = last_drag_id_,
                         .items = std::vector<protocol::OfferedItem>(items.size()),
                         .scanning = items.size(),
                         .broken = false,
                         .probes = {},
                         .offered_to = std::nullopt,
                         .parked = false,
                         .stopped = false,
                         .outcome = std::nullopt};
    for (std::uint32_t index = 0; index < items.size(); ++index) {
        outgoing_->items[index].content_type = std::move(items[index].content_type);
        const protocol::ObjectRef item{
            .kind = protocol::ObjectKind::Drag, .object_id = last_drag_id_, .item_index = index};
        if (!transfers_->offer(item, {std::move(items[index].path)}, audience)) {
            outgoing_->broken = true; // its paths are absolute, so this does not happen
        }
    }
    return Status<Error>::ok();
}

void Control::drag_stopped() {
    if (outgoing_ && outgoing_->parked) {
        outgoing_->stopped = true; // the platform may not see the button held: its outcome decides
        return;
    }
    withdraw_outgoing();
}

void Control::on_offer_ready(const transfer::OfferReady& ready) {
    if (ready.item.kind != protocol::ObjectKind::Drag || !outgoing_ ||
        ready.item.object_id != outgoing_->drag_id) {
        transfer_observer_.on_offer_ready(ready); // not this layer's drag
        return;
    }
    Outgoing& outgoing = *outgoing_;
    if (ready.item.item_index >= outgoing.items.size() || outgoing.scanning == 0) {
        return;
    }
    if (ready.error) {
        outgoing.broken = true; // the drag cannot travel as a whole
    } else {
        protocol::OfferedItem& item = outgoing.items[ready.item.item_index];
        item.kind = ready.folder ? protocol::ItemKind::Folder : protocol::ItemKind::File;
        item.byte_count = ready.total_bytes;
        item.name = ready.name;
        if (!protocol::check_type_id(item.content_type)) {
            item.content_type = ready.folder ? kFolderType : kDataType;
        }
    }
    if (--outgoing.scanning == 0) {
        if (!outgoing.broken && !protocol::encode_control(outgoing_offer(outgoing, 1))) {
            outgoing.broken = true; // its names do not fit in one message
        }
        answer_probes();
    }
}

protocol::DragOffer Control::outgoing_offer(const Outgoing& outgoing,
                                            std::uint32_t probe_id) const {
    return {.probe_id = probe_id,
            .drag_id = outgoing.drag_id,
            .source = self_,
            .items = outgoing.items};
}

void Control::receive_probe(const DeviceId& device, const protocol::DragProbe& probe) {
    const bool prober_controls = controlled_ && controlled_->controller == device;
    if (!prober_controls || !outgoing_ || outgoing_->broken || outgoing_->outcome) {
        send(device, protocol::DragOffer{
                         .probe_id = probe.probe_id, .drag_id = 0, .source = {}, .items = {}});
        return;
    }
    if (outgoing_->scanning > 0) {
        outgoing_->probes.emplace_back(device, probe.probe_id); // answered once measured
        return;
    }
    outgoing_->offered_to = device;
    send(device, outgoing_offer(*outgoing_, probe.probe_id));
}

void Control::answer_probes() {
    if (!outgoing_) {
        return;
    }
    const auto probes = std::exchange(outgoing_->probes, {});
    for (const auto& [device, probe_id] : probes) {
        receive_probe(device, {.probe_id = probe_id, .x = 0, .y = 0});
    }
}

void Control::receive_result(const DeviceId& device, const protocol::DragResult& result) {
    if (carried_ && carried_->offer.drag_id == result.drag_id) {
        carry(std::nullopt); // dropped or cancelled where the cursor is
    }
    // Only a drag that is away has an outcome: one that came back goes on here.
    if (device == self_ || !outgoing_ || outgoing_->drag_id != result.drag_id ||
        !outgoing_->parked) {
        return;
    }
    outgoing_->outcome = result.outcome;
    outgoing_->parked = false;
    observer_.on_drag_ended(result.drag_id, result.outcome);
    if (outgoing_->stopped) {
        withdraw_outgoing();
    }
}

void Control::abandon_parked() {
    if (!outgoing_) {
        return;
    }
    outgoing_->parked = false;
    outgoing_->outcome = protocol::DragOutcome::Cancelled;
    observer_.on_drag_ended(outgoing_->drag_id, protocol::DragOutcome::Cancelled);
    if (outgoing_->stopped) {
        withdraw_outgoing();
    }
}

void Control::withdraw_outgoing() {
    if (!outgoing_) {
        return;
    }
    const std::uint32_t drag_id = outgoing_->drag_id;
    const bool dropped = outgoing_->outcome == protocol::DragOutcome::Dropped;
    outgoing_.reset();
    if (dropped) {
        keep_dropped(drag_id); // the computer it was dropped on is still fetching
    } else {
        transfers_->withdraw(protocol::ObjectKind::Drag, drag_id);
    }
}

void Control::keep_dropped(std::uint32_t drag_id) {
    dropped_.push_back(drag_id);
    while (dropped_.size() > tunables_.kept_drags) {
        transfers_->withdraw(protocol::ObjectKind::Drag, dropped_.front());
        dropped_.pop_front();
    }
}

// --- As the target -------------------------------------------------------------------------------

void Control::arrive(Controlled& controlled, const protocol::DragOffer& offer) {
    if (offer.source == self_) {
        if (outgoing_ && outgoing_->drag_id == offer.drag_id && outgoing_->parked) {
            outgoing_->parked = false; // the drag goes on from here, its button still down
            outgoing_->offered_to = controlled.controller;
            static_cast<void>(controlled.held.button(kDragButton, protocol::ButtonAction::Down));
        }
        return;
    }
    incoming_ = Incoming{
        .controller = controlled.controller, .source = offer.source, .drag_id = offer.drag_id};
    observer_.on_drag_arrived({.source = offer.source,
                               .drag_id = offer.drag_id,
                               .items = offer.items,
                               .position = controlled.position});
    // The platform pressed the button: it is released like any other.
    static_cast<void>(controlled.held.button(kDragButton, protocol::ButtonAction::Down));
}

void Control::drop_finished(std::uint32_t drag_id, protocol::DragOutcome outcome) {
    if (!incoming_ || incoming_->drag_id != drag_id) {
        return; // withdrawn meanwhile
    }
    const Incoming ended = *incoming_;
    incoming_.reset();
    const protocol::DragResult result{.drag_id = drag_id, .outcome = outcome};
    send(ended.source, result);
    if (ended.controller == self_) {
        if (carried_ && carried_->offer.drag_id == drag_id) {
            carry(std::nullopt);
        }
    } else if (ended.controller != ended.source) {
        send(ended.controller, result);
    }
}

void Control::end_incoming(bool report) {
    if (!incoming_) {
        return;
    }
    const Incoming ended = *incoming_;
    incoming_.reset();
    observer_.on_drag_withdrawn(ended.drag_id);
    if (!report) {
        return;
    }
    const protocol::DragResult result{.drag_id = ended.drag_id,
                                      .outcome = protocol::DragOutcome::Cancelled};
    send(ended.source, result);
    if (ended.controller != self_ && ended.controller != ended.source) {
        send(ended.controller, result);
    }
}

} // namespace osdrop::control
