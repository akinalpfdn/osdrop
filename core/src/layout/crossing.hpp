#pragma once

// The crossing engine of the controller (DEVPLAN, "Roles and topology"): it follows this
// computer's mouse, keeps a virtual cursor while the input is on another computer, and decides
// every crossing, entry point and wall from the arrangement. Remote cursors move in their
// computer's native units, through its monitors as its own system would move them; this
// computer's movement is converted through logical units, so a hand movement covers about the
// same distance everywhere, times each computer's speed setting.

#include "layout/arrangement.hpp"
#include "layout/edge_push.hpp"
#include "layout/geometry.hpp"
#include "util/time.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace osdrop::layout {

/// Pointer speed on a computer, in thousandths of the controller's.
inline constexpr std::uint32_t kNormalSpeed = 1000;

/// Where the cursor is after an event.
struct CursorUpdate {
    DeviceId device{};    ///< the computer that has the cursor; this one when it is home
    Point position;       ///< native, on that computer
    bool crossed = false; ///< it has just arrived there
    double push = 0;      ///< progress of a push against an edge with a neighbour, 0 to 1
    /// The neighbour behind an edge the cursor pushes against while crossings are locked.
    std::optional<DeviceId> walled;
    bool operator==(const CursorUpdate&) const = default;
};

class CrossingEngine {
public:
    CrossingEngine(const DeviceId& self, PushConfig push);

    void set_push(PushConfig push);
    /// `permille` of the controller's speed while the cursor is on `device`.
    void set_speed(const DeviceId& device, std::uint32_t permille);
    /// The computers the cursor can reach now. When the computer that has the cursor is no
    /// longer among them, the cursor comes home to where it left, and the update says so.
    [[nodiscard]] std::optional<CursorUpdate> set_arrangement(Arrangement arrangement);
    /// While locked, every edge is a wall and the cursor stays on the computer it is on.
    void set_locked(bool locked);
    /// The computer that has the cursor reports where its cursor really is (an application moved
    /// it); the virtual cursor follows. Ignored while the cursor is home.
    void correct(Point position);

    /// One movement of this computer's mouse. `local` is where this computer's cursor is: moved
    /// by the system while the cursor is home, held in place while it is away. `delta` is the
    /// movement in this computer's native units.
    [[nodiscard]] CursorUpdate motion(TimePoint now, Point local, Point delta);
    /// Brings the cursor home to where it left (the other computer stopped answering, a hotkey).
    [[nodiscard]] CursorUpdate return_home();

    [[nodiscard]] bool away() const { return current_ != self_; }
    [[nodiscard]] const DeviceId& device() const { return current_; }
    [[nodiscard]] Point position() const { return position_; }

private:
    struct Push {
        Edge edge = Edge::Right;
        std::int64_t distance = 0; ///< logical
    };
    [[nodiscard]] CursorUpdate motion_home(TimePoint now, Point local, Point delta, Point logical);
    [[nodiscard]] CursorUpdate motion_away(TimePoint now, Point logical);
    [[nodiscard]] CursorUpdate push_against(TimePoint now, std::size_t monitor,
                                            const std::vector<Push>& pushes);
    [[nodiscard]] CursorUpdate here() const;

    DeviceId self_;
    EdgePush push_;
    Arrangement arrangement_;
    std::map<DeviceId, std::uint32_t> speeds_;
    DeviceId current_;
    Point position_;
    Point departure_; ///< where the cursor left this computer
    bool locked_ = false;
};

} // namespace osdrop::layout
