#include "layout/arrangement.hpp"

#include <cstdlib>
#include <utility>

namespace osdrop::layout {

namespace {

/// The facing edge of a candidate frame and whether the exit point lies along it.
struct Facing {
    std::int64_t edge = 0;
    bool spans = false;
};

Facing facing(const Rect& frame, Edge leaving, Point exit) {
    switch (leaving) {
    case Edge::Right:
        return {.edge = frame.left(), .spans = exit.y >= frame.top() && exit.y < frame.bottom()};
    case Edge::Left:
        return {.edge = frame.right(), .spans = exit.y >= frame.top() && exit.y < frame.bottom()};
    case Edge::Bottom:
        return {.edge = frame.top(), .spans = exit.x >= frame.left() && exit.x < frame.right()};
    case Edge::Top:
        return {.edge = frame.bottom(), .spans = exit.x >= frame.left() && exit.x < frame.right()};
    }
    return {};
}

std::int64_t line_of(const Rect& frame, Edge edge) {
    switch (edge) {
    case Edge::Right:
        return frame.right();
    case Edge::Left:
        return frame.left();
    case Edge::Bottom:
        return frame.bottom();
    case Edge::Top:
        return frame.top();
    }
    return 0;
}

/// The logical point just inside `frame` where the cursor enters through it, `exit` being the
/// global exit point and `offset` the frame's computer's place.
Point entry_point(const Rect& frame, Edge leaving, Point exit, Point offset) {
    const Point local = exit - offset;
    switch (leaving) {
    case Edge::Right:
        return {.x = frame.left(), .y = local.y};
    case Edge::Left:
        return {.x = frame.right() - 1, .y = local.y};
    case Edge::Bottom:
        return {.x = local.x, .y = frame.top()};
    case Edge::Top:
        return {.x = local.x, .y = frame.bottom() - 1};
    }
    return local;
}

} // namespace

Arrangement Arrangement::build(const Placements& placements, const ScreenMap& screens,
                               const std::set<DeviceId>& reachable) {
    Arrangement arrangement;
    for (const auto& [device, placement] : placements) {
        const auto known = screens.find(device);
        if (!placement.detached && reachable.contains(device) && known != screens.end() &&
            !known->second.empty()) {
            arrangement.add(device, known->second, placement.offset);
        }
    }
    return arrangement;
}

void Arrangement::add(const DeviceId& device, Screens screens, Point offset) {
    computers_[device] = Computer{.screens = std::move(screens), .offset = offset};
}

const Screens* Arrangement::screens(const DeviceId& device) const {
    const auto it = computers_.find(device);
    return it == computers_.end() ? nullptr : &it->second.screens;
}

std::optional<Entry> Arrangement::neighbour(const DeviceId& device, std::size_t monitor, Edge edge,
                                            Point exit) const {
    const auto from = computers_.find(device);
    if (from == computers_.end() || monitor >= from->second.screens.monitors().size()) {
        return std::nullopt;
    }
    const Monitor& leaving = from->second.screens.monitors()[monitor];
    // Rounding can put the last native unit one logical unit past the frame.
    const Point global_exit = leaving.frame.clamp(leaving.to_logical(exit)) + from->second.offset;
    const std::int64_t line = line_of(leaving.frame.moved(from->second.offset), edge);

    std::optional<Entry> best;
    std::int64_t best_gap = 0;
    for (const auto& [other, computer] : computers_) {
        if (other == device) {
            continue;
        }
        const auto& monitors = computer.screens.monitors();
        for (std::size_t i = 0; i < monitors.size(); ++i) {
            const Facing face = facing(monitors[i].frame.moved(computer.offset), edge, global_exit);
            const std::int64_t gap = std::abs(face.edge - line);
            if (!face.spans || gap > kSnapTolerance || (best && gap >= best_gap)) {
                continue;
            }
            const Point logical =
                entry_point(monitors[i].frame, edge, global_exit, computer.offset);
            best = Entry{.device = other,
                         .monitor = i,
                         .position = monitors[i].native.clamp(monitors[i].to_native(logical))};
            best_gap = gap;
        }
    }
    return best;
}

} // namespace osdrop::layout
