#include <osdrop/win/report_queue.hpp>

#include <algorithm>
#include <utility>
#include <variant>

namespace osdrop::win::input {
namespace {

std::int32_t clamped_sum(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(
        std::clamp<std::int64_t>(std::int64_t{a} + b, -kMaxCoordinate, kMaxCoordinate));
}

bool is_motion(const Report& report) {
    return std::holds_alternative<CapturedMotion>(report) ||
           std::holds_alternative<WatchedMotion>(report);
}

/// Adds `next` into `last` when both are motion of the same kind: the movement adds up, the
/// position is the newest and the time the earliest.
bool merged(Report& last, const Report& next) {
    if (auto* into = std::get_if<CapturedMotion>(&last)) {
        if (const auto* motion = std::get_if<CapturedMotion>(&next)) {
            into->dx = clamped_sum(into->dx, motion->dx);
            into->dy = clamped_sum(into->dy, motion->dy);
            return true;
        }
    }
    if (auto* into = std::get_if<WatchedMotion>(&last)) {
        if (const auto* motion = std::get_if<WatchedMotion>(&next)) {
            into->x = motion->x;
            into->y = motion->y;
            into->dx = clamped_sum(into->dx, motion->dx);
            into->dy = clamped_sum(into->dy, motion->dy);
            return true;
        }
    }
    return false;
}

} // namespace

ReportQueue::ReportQueue(std::function<void()> wake, std::size_t limit)
    : wake_(std::move(wake)), limit_(std::max<std::size_t>(limit, 1)) {}

void ReportQueue::push(const Report& report) {
    {
        const std::scoped_lock lock(mutex_);
        if (reports_.empty() || !merged(reports_.back(), report)) {
            reports_.push_back(report);
        }
        while (reports_.size() > limit_) {
            const auto oldest_motion = std::ranges::find_if(reports_, is_motion);
            if (oldest_motion == reports_.end()) {
                break; // only keys, buttons and scrolling: kept, whatever the count
            }
            reports_.erase(oldest_motion);
        }
    }
    if (wake_) {
        wake_();
    }
}

std::vector<Report> ReportQueue::take() {
    const std::scoped_lock lock(mutex_);
    std::vector<Report> taken(std::make_move_iterator(reports_.begin()),
                              std::make_move_iterator(reports_.end()));
    reports_.clear();
    return taken;
}

} // namespace osdrop::win::input
