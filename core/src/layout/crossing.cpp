#include "layout/crossing.hpp"

#include <utility>

namespace osdrop::layout {

namespace {

Point to_logical_delta(const Monitor& monitor, Point delta) {
    return {.x = scale(delta.x, monitor.frame.width, monitor.native.width),
            .y = scale(delta.y, monitor.frame.height, monitor.native.height)};
}

} // namespace

CrossingEngine::CrossingEngine(const DeviceId& self, PushConfig push)
    : self_(self), push_(push), current_(self) {}

void CrossingEngine::set_push(PushConfig push) {
    push_.set_config(push);
}

void CrossingEngine::set_speed(const DeviceId& device, std::uint32_t permille) {
    speeds_[device] = permille;
}

std::optional<CursorUpdate> CrossingEngine::set_arrangement(Arrangement arrangement) {
    arrangement_ = std::move(arrangement);
    push_.reset();
    if (!away()) {
        return std::nullopt;
    }
    const Screens* screens = arrangement_.screens(current_);
    if (screens == nullptr) {
        return return_home();
    }
    position_ = screens->clamp(position_);
    return std::nullopt;
}

CursorUpdate CrossingEngine::motion(TimePoint now, Point local, Point delta) {
    const Screens* own = arrangement_.screens(self_);
    const Point logical =
        own == nullptr ? delta : to_logical_delta(own->monitors()[own->monitor_near(local)], delta);
    return away() ? motion_away(now, logical) : motion_home(now, local, delta, logical);
}

CursorUpdate CrossingEngine::motion_home(TimePoint now, Point local, Point delta, Point logical) {
    position_ = local;
    const Screens* own = arrangement_.screens(self_);
    if (own == nullptr) {
        push_.reset();
        return here();
    }
    const std::size_t monitor = own->monitor_near(local);
    std::vector<Push> pushes;
    if (delta.x > 0 && own->at_outer_edge(monitor, Edge::Right, local)) {
        pushes.push_back({.edge = Edge::Right, .distance = logical.x});
    }
    if (delta.x < 0 && own->at_outer_edge(monitor, Edge::Left, local)) {
        pushes.push_back({.edge = Edge::Left, .distance = -logical.x});
    }
    if (delta.y > 0 && own->at_outer_edge(monitor, Edge::Bottom, local)) {
        pushes.push_back({.edge = Edge::Bottom, .distance = logical.y});
    }
    if (delta.y < 0 && own->at_outer_edge(monitor, Edge::Top, local)) {
        pushes.push_back({.edge = Edge::Top, .distance = -logical.y});
    }
    return push_against(now, monitor, pushes);
}

CursorUpdate CrossingEngine::motion_away(TimePoint now, Point logical) {
    const Screens* screens = arrangement_.screens(current_);
    if (screens == nullptr) {
        return return_home();
    }
    const Monitor& on = screens->monitors()[screens->monitor_near(position_)];
    const auto speed = speeds_.find(current_);
    const std::int64_t permille = speed == speeds_.end() ? kNormalSpeed : speed->second;
    const Point native{
        .x = scale(logical.x * permille, on.native.width, on.frame.width * kNormalSpeed),
        .y = scale(logical.y * permille, on.native.height, on.frame.height * kNormalSpeed)};
    const Motion moved = screens->move(position_, native);
    position_ = moved.position;
    const Monitor& arrived = screens->monitors()[moved.monitor];
    std::vector<Push> pushes;
    if (moved.blocked_x) {
        pushes.push_back(
            {.edge = *moved.blocked_x,
             .distance = scale(moved.overshoot_x, arrived.frame.width, arrived.native.width)});
    }
    if (moved.blocked_y) {
        pushes.push_back(
            {.edge = *moved.blocked_y,
             .distance = scale(moved.overshoot_y, arrived.frame.height, arrived.native.height)});
    }
    return push_against(now, moved.monitor, pushes);
}

CursorUpdate CrossingEngine::push_against(TimePoint now, std::size_t monitor,
                                          const std::vector<Push>& pushes) {
    // At a corner, the edge pushed harder that has a neighbour.
    std::optional<Push> chosen;
    std::optional<Entry> target;
    for (const Push& push : pushes) {
        if (chosen && push.distance <= chosen->distance) {
            continue;
        }
        if (auto entry = arrangement_.neighbour(current_, monitor, push.edge, position_)) {
            chosen = push;
            target = std::move(entry);
        }
    }
    if (!chosen || !target) {
        push_.reset(); // a wall, or not pushing
        return here();
    }
    const EdgeContact contact{.device = current_, .monitor = monitor, .edge = chosen->edge};
    if (!push_.push(now, contact, chosen->distance)) {
        CursorUpdate update = here();
        update.push = push_.progress(now);
        return update;
    }
    if (!away()) {
        departure_ = position_;
    }
    current_ = target->device;
    position_ = target->position;
    return {.device = current_, .position = position_, .crossed = true, .push = 0};
}

CursorUpdate CrossingEngine::return_home() {
    if (!away()) {
        return here();
    }
    current_ = self_;
    position_ = departure_;
    push_.reset();
    return {.device = current_, .position = position_, .crossed = true, .push = 0};
}

CursorUpdate CrossingEngine::here() const {
    return {.device = current_, .position = position_, .crossed = false, .push = 0};
}

} // namespace osdrop::layout
