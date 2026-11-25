#include "layout/document.hpp"

#include <array>
#include <cstdlib>
#include <optional>
#include <tuple>

namespace osdrop::layout {

namespace {

/// Two frames meet: opposite edges within the snap tolerance and a shared stretch along them.
/// Frames that overlap meet too; the overlap is its own problem.
bool meet(const Rect& a, const Rect& b) {
    if (a.overlaps(b)) {
        return true;
    }
    const bool side_by_side = std::abs(a.right() - b.left()) <= kSnapTolerance ||
                              std::abs(b.right() - a.left()) <= kSnapTolerance;
    const bool stacked = std::abs(a.bottom() - b.top()) <= kSnapTolerance ||
                         std::abs(b.bottom() - a.top()) <= kSnapTolerance;
    return (side_by_side && shared_length(a.top(), a.bottom(), b.top(), b.bottom()) > 0) ||
           (stacked && shared_length(a.left(), a.right(), b.left(), b.right()) > 0);
}

bool any_pair(const std::vector<Rect>& a, const std::vector<Rect>& b,
              bool (*test)(const Rect&, const Rect&)) {
    for (const Rect& ra : a) {
        for (const Rect& rb : b) {
            if (test(ra, rb)) {
                return true;
            }
        }
    }
    return false;
}

struct Attached {
    DeviceId device;
    std::vector<Rect> frames;
};

std::vector<Attached> attached(const Placements& placements, const ScreenMap& screens) {
    std::vector<Attached> result;
    for (const auto& [device, placement] : placements) {
        const auto known = screens.find(device);
        if (!placement.detached && known != screens.end() && !known->second.empty()) {
            result.push_back({.device = device, .frames = global_frames(known->second, placement)});
        }
    }
    return result;
}

/// Groups of computers that touch each other, each in device ID order, in order of their first
/// computer.
std::vector<std::vector<std::size_t>> touching_groups(const std::vector<Attached>& computers) {
    std::vector<std::vector<std::size_t>> groups;
    std::vector<bool> seen(computers.size(), false);
    for (std::size_t start = 0; start < computers.size(); ++start) {
        if (seen[start]) {
            continue;
        }
        std::vector<std::size_t> group{start};
        seen[start] = true;
        for (std::size_t next = 0; next < group.size(); ++next) {
            for (std::size_t other = 0; other < computers.size(); ++other) {
                if (!seen[other] &&
                    any_pair(computers[group[next]].frames, computers[other].frames, meet)) {
                    seen[other] = true;
                    group.push_back(other);
                }
            }
        }
        groups.push_back(std::move(group));
    }
    return groups;
}

/// Placements as comparable values, for a deterministic tie break.
std::vector<std::tuple<DeviceId, std::int64_t, std::int64_t, bool>>
canonical(const Placements& placements) {
    std::vector<std::tuple<DeviceId, std::int64_t, std::int64_t, bool>> values;
    values.reserve(placements.size());
    for (const auto& [device, placement] : placements) {
        values.emplace_back(device, placement.offset.x, placement.offset.y, placement.detached);
    }
    return values;
}

} // namespace

bool LayoutDocument::merge(const LayoutDocument& other) {
    const auto key = [](const LayoutDocument& document) {
        return std::tie(document.revision, document.author);
    };
    // One author never writes two documents with the same revision; should a faulty member do
    // so, every member still keeps the same one.
    if (key(other) > key(*this) ||
        (key(other) == key(*this) && canonical(other.placements) < canonical(placements))) {
        *this = other;
        return true;
    }
    return false;
}

LayoutDocument LayoutDocument::from_message(const protocol::LayoutState& message) {
    LayoutDocument document{
        .revision = message.revision, .author = message.author, .placements = {}};
    for (const protocol::Placement& p : message.placements) {
        document.placements[p.device_id] = {.offset = {.x = p.x, .y = p.y}, .detached = p.detached};
    }
    return document;
}

protocol::LayoutState LayoutDocument::to_message() const {
    protocol::LayoutState message{.revision = revision, .author = author, .placements = {}};
    message.placements.reserve(placements.size());
    for (const auto& [device, placement] : placements) {
        message.placements.push_back({.device_id = device,
                                      .x = static_cast<std::int32_t>(placement.offset.x),
                                      .y = static_cast<std::int32_t>(placement.offset.y),
                                      .detached = placement.detached});
    }
    return message;
}

std::vector<Rect> global_frames(const Screens& screens, const Placement& placement) {
    std::vector<Rect> frames;
    frames.reserve(screens.monitors().size());
    for (const Monitor& monitor : screens.monitors()) {
        frames.push_back(monitor.frame.moved(placement.offset));
    }
    return frames;
}

std::vector<Problem> check(const Placements& placements, const ScreenMap& screens) {
    const std::vector<Attached> computers = attached(placements, screens);
    std::vector<Problem> problems;
    for (std::size_t a = 0; a < computers.size(); ++a) {
        for (std::size_t b = a + 1; b < computers.size(); ++b) {
            if (any_pair(computers[a].frames, computers[b].frames,
                         [](const Rect& x, const Rect& y) { return x.overlaps(y); })) {
                problems.push_back({.kind = ProblemKind::Overlap,
                                    .device = computers[a].device,
                                    .other = computers[b].device});
            }
        }
    }
    const auto groups = touching_groups(computers);
    std::size_t largest = 0;
    for (std::size_t i = 1; i < groups.size(); ++i) {
        if (groups[i].size() > groups[largest].size()) {
            largest = i;
        }
    }
    for (std::size_t i = 0; i < groups.size(); ++i) {
        if (i == largest) {
            continue;
        }
        for (const std::size_t member : groups[i]) {
            problems.push_back({.kind = ProblemKind::Unreachable,
                                .device = computers[member].device,
                                .other = {}});
        }
    }
    return problems;
}

Point place_beside(const Placements& placements, const ScreenMap& screens, const Screens& own) {
    const std::vector<Attached> computers = attached(placements, screens);
    if (computers.empty() || own.empty()) {
        return {};
    }
    const Rect* rightmost = nullptr;
    for (const Attached& computer : computers) {
        for (const Rect& frame : computer.frames) {
            const bool further = rightmost == nullptr || frame.right() > rightmost->right() ||
                                 (frame.right() == rightmost->right() && frame.y < rightmost->y);
            if (further) {
                rightmost = &frame;
            }
        }
    }
    const Monitor* leftmost = nullptr;
    for (const Monitor& monitor : own.monitors()) {
        if (leftmost == nullptr || std::tie(monitor.frame.x, monitor.frame.y) <
                                       std::tie(leftmost->frame.x, leftmost->frame.y)) {
            leftmost = &monitor;
        }
    }
    if (rightmost == nullptr || leftmost == nullptr) {
        return {};
    }
    return {.x = rightmost->right() - leftmost->frame.x, .y = rightmost->top() - leftmost->frame.y};
}

namespace {

enum Side : std::uint8_t { kRight, kLeft, kBelow, kAbove };

/// How far a placed computer's monitors reach toward `side`, counting only those across from the
/// span [from, to) on the other axis; all of them when none is.
std::int64_t reach(const Screens& screens, Point offset, Side side, std::int64_t from,
                   std::int64_t to) {
    const bool horizontal = side == kRight || side == kLeft;
    std::optional<std::int64_t> facing;
    std::optional<std::int64_t> any;
    for (const Monitor& monitor : screens.monitors()) {
        const Rect frame = monitor.frame.moved(offset);
        const std::int64_t edge = side == kRight   ? frame.right()
                                  : side == kLeft  ? frame.left()
                                  : side == kBelow ? frame.bottom()
                                                   : frame.top();
        const bool outward = side == kRight || side == kBelow;
        const auto further = [&](const std::optional<std::int64_t>& best) {
            return !best || (outward ? edge > *best : edge < *best);
        };
        if (further(any)) {
            any = edge;
        }
        const bool across = horizontal ? shared_length(frame.top(), frame.bottom(), from, to) > 0
                                       : shared_length(frame.left(), frame.right(), from, to) > 0;
        if (across && further(facing)) {
            facing = edge;
        }
    }
    return facing.value_or(any.value_or(0));
}

} // namespace

Placements follow_screens(const Placements& placements, const ScreenMap& screens,
                          const DeviceId& device, const Screens& before, const Screens& after) {
    const auto self = placements.find(device);
    if (self == placements.end() || before.empty() || after.empty()) {
        return placements;
    }
    const Point origin = self->second.offset;
    const Rect was = before.logical_bounds().moved(origin);
    Placements moved = placements;
    for (auto& [other, placement] : moved) {
        const auto known = screens.find(other);
        if (other == device || known == screens.end() || known->second.empty()) {
            continue;
        }
        const Rect bounds = known->second.logical_bounds().moved(placement.offset);
        // How far apart they were on each side of the computer; negative: no gap.
        const std::array<std::int64_t, 4> gaps{
            bounds.left() - was.right(), was.left() - bounds.right(), bounds.top() - was.bottom(),
            was.top() - bounds.bottom()};
        const auto side = static_cast<Side>(std::ranges::max_element(gaps) - gaps.begin());
        if (gaps.at(side) < 0) {
            continue; // they overlapped: nothing to keep
        }
        const bool horizontal = side == kRight || side == kLeft;
        const std::int64_t from = horizontal ? bounds.top() : bounds.left();
        const std::int64_t to = horizontal ? bounds.bottom() : bounds.right();
        const std::int64_t shift =
            reach(after, origin, side, from, to) - reach(before, origin, side, from, to);
        placement.offset =
            placement.offset + (horizontal ? Point{.x = shift, .y = 0} : Point{.x = 0, .y = shift});
    }
    return moved;
}

} // namespace osdrop::layout
