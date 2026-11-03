#pragma once

// The arrangement the cursor moves through: the computers it can reach now, each with its
// monitors and its place in the shared space, and the rule that decides where the cursor
// continues when it leaves a computer through an outer edge.

#include "layout/document.hpp"
#include "layout/geometry.hpp"
#include "layout/screens.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <set>

namespace osdrop::layout {

/// Where the cursor continues: a computer, its monitor and a native point on it.
struct Entry {
    DeviceId device{};
    std::size_t monitor = 0;
    Point position;
    bool operator==(const Entry&) const = default;
};

class Arrangement {
public:
    /// The placed, attached computers among `reachable` whose screens are known.
    [[nodiscard]] static Arrangement build(const Placements& placements, const ScreenMap& screens,
                                           const std::set<DeviceId>& reachable);

    void add(const DeviceId& device, Screens screens, Point offset);
    [[nodiscard]] const Screens* screens(const DeviceId& device) const;

    /// Where the cursor continues when it leaves `device` through `edge` of `monitor` at the
    /// native point `exit`: on the facing monitor of another computer whose edge is within the
    /// snap tolerance and whose span contains the exit point, the nearest first, at the same
    /// position along the edge. No value: a wall.
    [[nodiscard]] std::optional<Entry> neighbour(const DeviceId& device, std::size_t monitor,
                                                 Edge edge, Point exit) const;

private:
    struct Computer {
        Screens screens;
        Point offset;
    };
    std::map<DeviceId, Computer> computers_;
};

} // namespace osdrop::layout
