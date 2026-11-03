#pragma once

// Edge push: the cursor crosses to the neighbouring computer only after the user has kept pushing
// against the edge for a while and far enough, so that reaching for something at the edge of the
// screen does not take the input away (DEVPLAN: about one second, adjustable, or immediate).

#include "layout/document.hpp"
#include "layout/geometry.hpp"
#include "util/time.hpp"

#include <cstddef>
#include <cstdint>

namespace osdrop::layout {

struct PushConfig {
    Milliseconds time{1000};            ///< how long the push must last
    std::int64_t distance = 32 * kUnit; ///< how far, in logical units, it must go past the edge
    Milliseconds gap{300};              ///< a pause longer than this starts over
    bool immediate = false;             ///< cross as soon as the cursor pushes past the edge
};

/// The edge being pushed: a computer, its monitor and the side.
struct EdgeContact {
    DeviceId device{};
    std::size_t monitor = 0;
    Edge edge = Edge::Right;
    bool operator==(const EdgeContact&) const = default;
};

class EdgePush {
public:
    explicit EdgePush(PushConfig config) : config_(config) {}

    void set_config(PushConfig config);
    /// A movement that went `distance` (logical, positive) past `contact`. True when the push is
    /// complete: the cursor crosses, and the next push starts over.
    bool push(TimePoint now, const EdgeContact& contact, std::int64_t distance);
    /// The cursor left the edge or stopped pushing.
    void reset() { active_ = false; }
    /// How far the current push has come, from 0 to 1, for feedback.
    [[nodiscard]] double progress(TimePoint now) const;

private:
    PushConfig config_;
    bool active_ = false;
    EdgeContact contact_;
    TimePoint started_{};
    TimePoint last_{};
    std::int64_t pushed_ = 0;
};

} // namespace osdrop::layout
