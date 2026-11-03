#pragma once

// One computer's monitors (docs/PROTOCOL.md, "ScreenReport"). Each has a frame in the computer's
// logical desktop space, where the shared arrangement places it, and a rectangle in native units,
// where the computer's own cursor moves (macOS points, Windows physical pixels). The cursor moves
// between a computer's monitors in native units, as the platform moves it, and leaves the
// computer only through an outer edge: one that no other monitor of the computer continues.

#include "layout/geometry.hpp"
#include "protocol/control_messages.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace osdrop::layout {

struct Monitor {
    protocol::Id key{};
    Rect frame;  ///< logical
    Rect native; ///< native
    std::uint16_t pixel_width = 0;
    std::uint16_t pixel_height = 0;
    bool primary = false;

    [[nodiscard]] Point to_logical(Point native_point) const;
    [[nodiscard]] Point to_native(Point logical_point) const;
    bool operator==(const Monitor&) const = default;
};

/// Where a movement left the cursor, and how far it went past the outer edges that stopped it.
struct Motion {
    Point position;
    std::size_t monitor = 0;
    std::optional<Edge> blocked_x;
    std::optional<Edge> blocked_y;
    std::int64_t overshoot_x = 0; ///< native, positive
    std::int64_t overshoot_y = 0;
};

class Screens {
public:
    Screens() = default;
    /// Monitors with positive sizes, as a decoded ScreenReport guarantees.
    explicit Screens(std::vector<Monitor> monitors);

    [[nodiscard]] static Screens from_report(const protocol::ScreenReport& report);
    /// Only for screens that are not empty.
    [[nodiscard]] protocol::ScreenReport to_report() const;

    [[nodiscard]] const std::vector<Monitor>& monitors() const { return monitors_; }
    [[nodiscard]] bool empty() const { return monitors_.empty(); }

    /// The first monitor containing the native point.
    [[nodiscard]] std::optional<std::size_t> monitor_at(Point native) const;
    /// The monitor containing the native point, or the nearest one. Not for empty screens.
    [[nodiscard]] std::size_t monitor_near(Point native) const;
    /// The nearest native point on a monitor.
    [[nodiscard]] Point clamp(Point native) const;
    /// Moves the cursor by a native delta: straight to a point on any monitor, otherwise
    /// horizontally and then vertically through monitors that continue each other, stopping at
    /// outer edges. Not for empty screens.
    [[nodiscard]] Motion move(Point from, Point delta) const;
    /// The native point is within one unit of the monitor's edge and no monitor of this computer
    /// continues beyond it there.
    [[nodiscard]] bool at_outer_edge(std::size_t monitor, Edge edge, Point native) const;
    /// The smallest rectangle around every frame.
    [[nodiscard]] Rect logical_bounds() const;

    bool operator==(const Screens&) const = default;

private:
    struct Slide {
        std::int64_t position = 0;
        std::size_t monitor = 0;
        std::optional<Edge> blocked;
        std::int64_t overshoot = 0;
    };
    [[nodiscard]] Slide slide_x(Point from, std::size_t monitor, std::int64_t delta) const;
    [[nodiscard]] Slide slide_y(Point from, std::size_t monitor, std::int64_t delta) const;

    std::vector<Monitor> monitors_;
};

/// Frames for monitors known in native units only, as on Windows, where monitors with different
/// scales share no logical space. Each monitor brings its native rectangle and its logical size
/// (frame width and height). Monitors that touch in native units touch in the result, aligned
/// where they meet; the primary monitor's frame starts at its native origin, scaled. Monitors
/// that touch none of the others keep their native position, scaled like the primary.
[[nodiscard]] std::vector<Monitor> derive_frames(std::vector<Monitor> monitors);

} // namespace osdrop::layout
