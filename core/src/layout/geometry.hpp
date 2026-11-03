#pragma once

// Plane geometry for the arrangement, in coord units: 1/16 of a logical or native unit
// (docs/PROTOCOL.md, "Conventions"). Values are 64-bit so that sums and scaled coordinates never
// overflow. Rectangles are half-open: a point on the right or bottom edge is outside.

#include <algorithm>
#include <cstdint>

namespace osdrop::layout {

/// One logical or native unit (a point, a device-independent pixel or a physical pixel).
inline constexpr std::int64_t kUnit = 16;

struct Point {
    std::int64_t x = 0;
    std::int64_t y = 0;
    bool operator==(const Point&) const = default;
};

inline Point operator+(Point a, Point b) {
    return {.x = a.x + b.x, .y = a.y + b.y};
}
inline Point operator-(Point a, Point b) {
    return {.x = a.x - b.x, .y = a.y - b.y};
}

struct Rect {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t width = 0;
    std::int64_t height = 0;

    [[nodiscard]] std::int64_t left() const { return x; }
    [[nodiscard]] std::int64_t top() const { return y; }
    [[nodiscard]] std::int64_t right() const { return x + width; }
    [[nodiscard]] std::int64_t bottom() const { return y + height; }
    [[nodiscard]] Point origin() const { return {.x = x, .y = y}; }

    [[nodiscard]] bool contains(Point p) const {
        return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
    }
    /// The nearest point inside.
    [[nodiscard]] Point clamp(Point p) const {
        return {.x = std::clamp(p.x, x, right() - 1), .y = std::clamp(p.y, y, bottom() - 1)};
    }
    /// The interiors intersect: touching edges do not overlap.
    [[nodiscard]] bool overlaps(const Rect& other) const {
        return x < other.right() && other.x < right() && y < other.bottom() && other.y < bottom();
    }
    [[nodiscard]] Rect moved(Point offset) const {
        return {.x = x + offset.x, .y = y + offset.y, .width = width, .height = height};
    }
    bool operator==(const Rect&) const = default;
};

enum class Edge : std::uint8_t { Left, Right, Top, Bottom };

/// The length two ranges [a_start, a_end) and [b_start, b_end) share; 0 when they do not.
[[nodiscard]] inline std::int64_t shared_length(std::int64_t a_start, std::int64_t a_end,
                                                std::int64_t b_start, std::int64_t b_end) {
    return std::max<std::int64_t>(0, std::min(a_end, b_end) - std::max(a_start, b_start));
}

/// value × numerator / denominator, rounded to the nearest (halves away from zero). The
/// denominator is positive.
[[nodiscard]] inline std::int64_t scale(std::int64_t value, std::int64_t numerator,
                                        std::int64_t denominator) {
    const std::int64_t product = value * numerator;
    const std::int64_t half = denominator / 2;
    return product >= 0 ? (product + half) / denominator : -((-product + half) / denominator);
}

} // namespace osdrop::layout
