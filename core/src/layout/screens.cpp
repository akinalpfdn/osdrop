#include "layout/screens.hpp"

#include <deque>
#include <limits>
#include <utility>

namespace osdrop::layout {

namespace {

Rect frame_of(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    return {.x = x, .y = y, .width = width, .height = height};
}

/// The squared distance from a point to the nearest point of a rectangle.
std::int64_t distance_squared(const Rect& rect, Point p) {
    const Point nearest = rect.clamp(p);
    const std::int64_t dx = nearest.x - p.x;
    const std::int64_t dy = nearest.y - p.y;
    return (dx * dx) + (dy * dy);
}

/// Where `next`'s frame goes when its native rectangle touches `placed`'s: the shared edge
/// coincides, and `next` starts along it as far from `placed` as it does in native units, scaled
/// by `placed`.
std::optional<Point> beside(const Monitor& placed, const Monitor& next) {
    const Rect& a = placed.native;
    const Rect& b = next.native;
    const Rect& frame = placed.frame;
    const bool rows_meet = shared_length(a.top(), a.bottom(), b.top(), b.bottom()) > 0;
    const bool columns_meet = shared_length(a.left(), a.right(), b.left(), b.right()) > 0;
    const std::int64_t along_y = frame.y + scale(b.top() - a.top(), frame.height, a.height);
    const std::int64_t along_x = frame.x + scale(b.left() - a.left(), frame.width, a.width);
    if (rows_meet && b.left() == a.right()) {
        return Point{.x = frame.right(), .y = along_y};
    }
    if (rows_meet && b.right() == a.left()) {
        return Point{.x = frame.x - next.frame.width, .y = along_y};
    }
    if (columns_meet && b.top() == a.bottom()) {
        return Point{.x = along_x, .y = frame.bottom()};
    }
    if (columns_meet && b.bottom() == a.top()) {
        return Point{.x = along_x, .y = frame.y - next.frame.height};
    }
    return std::nullopt;
}

} // namespace

Point Monitor::to_logical(Point native_point) const {
    return {.x = frame.x + scale(native_point.x - native.x, frame.width, native.width),
            .y = frame.y + scale(native_point.y - native.y, frame.height, native.height)};
}

Point Monitor::to_native(Point logical_point) const {
    return {.x = native.x + scale(logical_point.x - frame.x, native.width, frame.width),
            .y = native.y + scale(logical_point.y - frame.y, native.height, frame.height)};
}

Screens::Screens(std::vector<Monitor> monitors) : monitors_(std::move(monitors)) {}

Screens Screens::from_report(const protocol::ScreenReport& report) {
    std::vector<Monitor> monitors;
    monitors.reserve(report.monitors.size());
    for (const protocol::Monitor& m : report.monitors) {
        monitors.push_back(
            {.key = m.key,
             .frame = frame_of(m.x, m.y, m.width, m.height),
             .native = frame_of(m.native_x, m.native_y, m.native_width, m.native_height),
             .pixel_width = m.pixel_width,
             .pixel_height = m.pixel_height,
             .primary = m.primary});
    }
    return Screens(std::move(monitors));
}

protocol::ScreenReport Screens::to_report() const {
    protocol::ScreenReport report;
    report.monitors.reserve(monitors_.size());
    for (const Monitor& m : monitors_) {
        report.monitors.push_back({.key = m.key,
                                   .x = static_cast<std::int32_t>(m.frame.x),
                                   .y = static_cast<std::int32_t>(m.frame.y),
                                   .width = static_cast<std::int32_t>(m.frame.width),
                                   .height = static_cast<std::int32_t>(m.frame.height),
                                   .native_x = static_cast<std::int32_t>(m.native.x),
                                   .native_y = static_cast<std::int32_t>(m.native.y),
                                   .native_width = static_cast<std::int32_t>(m.native.width),
                                   .native_height = static_cast<std::int32_t>(m.native.height),
                                   .pixel_width = m.pixel_width,
                                   .pixel_height = m.pixel_height,
                                   .primary = m.primary});
    }
    return report;
}

std::optional<std::size_t> Screens::monitor_at(Point native) const {
    for (std::size_t i = 0; i < monitors_.size(); ++i) {
        if (monitors_[i].native.contains(native)) {
            return i;
        }
    }
    return std::nullopt;
}

std::size_t Screens::monitor_near(Point native) const {
    std::size_t nearest = 0;
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    for (std::size_t i = 0; i < monitors_.size(); ++i) {
        const std::int64_t distance = distance_squared(monitors_[i].native, native);
        if (distance < best) {
            best = distance;
            nearest = i;
        }
    }
    return nearest;
}

Point Screens::clamp(Point native) const {
    return monitors_[monitor_near(native)].native.clamp(native);
}

Screens::Slide Screens::slide_x(Point from, std::size_t monitor, std::int64_t delta) const {
    Slide slide{.position = from.x, .monitor = monitor, .blocked = std::nullopt, .overshoot = 0};
    std::int64_t remaining = delta;
    while (remaining != 0) {
        const Rect& rect = monitors_[slide.monitor].native;
        const bool right = remaining > 0;
        const std::int64_t room =
            right ? rect.right() - 1 - slide.position : slide.position - rect.left();
        if ((right ? remaining : -remaining) <= room) {
            slide.position += remaining;
            break;
        }
        // One unit past the edge: another monitor of this computer, or an outer edge.
        const std::int64_t beyond = right ? rect.right() : rect.left() - 1;
        remaining -= right ? room + 1 : -(room + 1);
        const auto next = monitor_at({.x = beyond, .y = from.y});
        if (!next) {
            slide.position = right ? rect.right() - 1 : rect.left();
            slide.blocked = right ? Edge::Right : Edge::Left;
            slide.overshoot = (right ? remaining : -remaining) + 1;
            break;
        }
        slide.position = beyond;
        slide.monitor = *next;
    }
    return slide;
}

Screens::Slide Screens::slide_y(Point from, std::size_t monitor, std::int64_t delta) const {
    Slide slide{.position = from.y, .monitor = monitor, .blocked = std::nullopt, .overshoot = 0};
    std::int64_t remaining = delta;
    while (remaining != 0) {
        const Rect& rect = monitors_[slide.monitor].native;
        const bool down = remaining > 0;
        const std::int64_t room =
            down ? rect.bottom() - 1 - slide.position : slide.position - rect.top();
        if ((down ? remaining : -remaining) <= room) {
            slide.position += remaining;
            break;
        }
        const std::int64_t beyond = down ? rect.bottom() : rect.top() - 1;
        remaining -= down ? room + 1 : -(room + 1);
        const auto next = monitor_at({.x = from.x, .y = beyond});
        if (!next) {
            slide.position = down ? rect.bottom() - 1 : rect.top();
            slide.blocked = down ? Edge::Bottom : Edge::Top;
            slide.overshoot = (down ? remaining : -remaining) + 1;
            break;
        }
        slide.position = beyond;
        slide.monitor = *next;
    }
    return slide;
}

Motion Screens::move(Point from, Point delta) const {
    const Point start = clamp(from);
    const Point target = start + delta;
    if (const auto hit = monitor_at(target)) {
        return {.position = target, .monitor = *hit};
    }
    const Slide x = slide_x(start, monitor_near(start), delta.x);
    const Slide y = slide_y({.x = x.position, .y = start.y}, x.monitor, delta.y);
    return {.position = {.x = x.position, .y = y.position},
            .monitor = y.monitor,
            .blocked_x = x.blocked,
            .blocked_y = y.blocked,
            .overshoot_x = x.overshoot,
            .overshoot_y = y.overshoot};
}

bool Screens::at_outer_edge(std::size_t monitor, Edge edge, Point native) const {
    const Rect& rect = monitors_[monitor].native;
    switch (edge) {
    case Edge::Left:
        return native.x < rect.left() + kUnit && !monitor_at({.x = rect.left() - 1, .y = native.y});
    case Edge::Right:
        return native.x >= rect.right() - kUnit && !monitor_at({.x = rect.right(), .y = native.y});
    case Edge::Top:
        return native.y < rect.top() + kUnit && !monitor_at({.x = native.x, .y = rect.top() - 1});
    case Edge::Bottom:
        return native.y >= rect.bottom() - kUnit &&
               !monitor_at({.x = native.x, .y = rect.bottom()});
    }
    return false;
}

Rect Screens::logical_bounds() const {
    if (monitors_.empty()) {
        return {};
    }
    std::int64_t left = monitors_.front().frame.left();
    std::int64_t top = monitors_.front().frame.top();
    std::int64_t right = monitors_.front().frame.right();
    std::int64_t bottom = monitors_.front().frame.bottom();
    for (const Monitor& m : monitors_) {
        left = std::min(left, m.frame.left());
        top = std::min(top, m.frame.top());
        right = std::max(right, m.frame.right());
        bottom = std::max(bottom, m.frame.bottom());
    }
    return {.x = left, .y = top, .width = right - left, .height = bottom - top};
}

std::vector<Monitor> derive_frames(std::vector<Monitor> monitors) {
    if (monitors.empty()) {
        return monitors;
    }
    std::size_t primary = 0;
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        if (monitors[i].primary) {
            primary = i;
            break;
        }
    }
    const auto scaled_like = [&](Monitor& m, const Monitor& reference) {
        m.frame.x = scale(m.native.x, reference.frame.width, reference.native.width);
        m.frame.y = scale(m.native.y, reference.frame.height, reference.native.height);
    };
    std::vector<bool> placed(monitors.size(), false);
    scaled_like(monitors[primary], monitors[primary]);
    placed[primary] = true;
    std::deque<std::size_t> queue{primary};
    while (!queue.empty()) {
        const std::size_t current = queue.front();
        queue.pop_front();
        for (std::size_t next = 0; next < monitors.size(); ++next) {
            if (placed[next]) {
                continue;
            }
            if (const auto origin = beside(monitors[current], monitors[next])) {
                monitors[next].frame.x = origin->x;
                monitors[next].frame.y = origin->y;
                placed[next] = true;
                queue.push_back(next);
            }
        }
    }
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        if (!placed[i]) {
            scaled_like(monitors[i], monitors[primary]);
        }
    }
    return monitors;
}

} // namespace osdrop::layout
