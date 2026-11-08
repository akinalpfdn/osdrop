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

} // namespace

ReportQueue::ReportQueue(std::function<void()> wake, std::size_t limit)
    : wake_(std::move(wake)), limit_(std::max<std::size_t>(limit, 1)) {}

void ReportQueue::push(const Report& report) {
    {
        const std::scoped_lock lock(mutex_);
        const auto* motion = std::get_if<CapturedMotion>(&report);
        auto* last = reports_.empty() ? nullptr : std::get_if<CapturedMotion>(&reports_.back());
        if (motion != nullptr && last != nullptr) {
            last->dx = clamped_sum(last->dx, motion->dx);
            last->dy = clamped_sum(last->dy, motion->dy);
        } else {
            reports_.push_back(report);
        }
        while (reports_.size() > limit_) {
            const auto oldest_motion = std::ranges::find_if(reports_, [](const Report& queued) {
                return std::holds_alternative<CapturedMotion>(queued);
            });
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
