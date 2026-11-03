#pragma once

// The layout document (docs/PROTOCOL.md, "Arrangement"): where each computer's logical desktop
// sits in the shared arrangement. It is replaced as a whole and the newer (revision, author) wins,
// so members converge whatever the order of messages. Checks for the arrangement editor live here
// too: monitors of different computers must not overlap, and every computer must touch the
// others or be detached.

#include "layout/geometry.hpp"
#include "layout/screens.hpp"
#include "protocol/control_messages.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace osdrop::layout {

using DeviceId = protocol::Id;

/// How far apart two edges may be and still meet: the arrangement editor snaps, but a computer's
/// monitors can change size a little after it placed them.
inline constexpr std::int64_t kSnapTolerance = 8 * kUnit;

struct Placement {
    Point offset; ///< where the computer's logical origin sits in the arrangement
    bool detached = false;
    bool operator==(const Placement&) const = default;
};

using Placements = std::map<DeviceId, Placement>;
using ScreenMap = std::map<DeviceId, Screens>;

struct LayoutDocument {
    std::uint64_t revision = 0;
    DeviceId author{};
    Placements placements;

    /// Takes `other` if it is newer by (revision, author); for the same revision and author, the
    /// smaller placements. Returns whether this changed.
    bool merge(const LayoutDocument& other);

    [[nodiscard]] static LayoutDocument from_message(const protocol::LayoutState& message);
    /// Only for documents with 1 to 64 placements.
    [[nodiscard]] protocol::LayoutState to_message() const;

    bool operator==(const LayoutDocument&) const = default;
};

enum class ProblemKind : std::uint8_t {
    Overlap = 1,     ///< `device`'s monitors overlap `other`'s
    Unreachable = 2, ///< `device` touches none of the others and is not detached
};

struct Problem {
    ProblemKind kind = ProblemKind::Overlap;
    DeviceId device{};
    DeviceId other{}; ///< zeros for Unreachable
    bool operator==(const Problem&) const = default;
};

/// Problems of placed, attached computers whose screens are known. With more than one group of
/// computers that touch each other, every computer outside the largest group (ties: the one with
/// the lowest device ID) is unreachable.
[[nodiscard]] std::vector<Problem> check(const Placements& placements, const ScreenMap& screens);

/// Where a computer that is not placed yet goes: to the right of everything placed and attached,
/// its leftmost monitor touching the rightmost one, tops aligned. At the origin when nothing is
/// placed.
[[nodiscard]] Point place_beside(const Placements& placements, const ScreenMap& screens,
                                 const Screens& own);

/// The global frame of every monitor of a placed computer.
[[nodiscard]] std::vector<Rect> global_frames(const Screens& screens, const Placement& placement);

} // namespace osdrop::layout
