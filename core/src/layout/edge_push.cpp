#include "layout/edge_push.hpp"

#include <algorithm>
#include <chrono>

namespace osdrop::layout {

void EdgePush::set_config(PushConfig config) {
    config_ = config;
    active_ = false;
}

bool EdgePush::push(TimePoint now, const EdgeContact& contact, std::int64_t distance) {
    if (config_.immediate) {
        active_ = false;
        return true;
    }
    if (!active_ || contact != contact_ || now - last_ > config_.gap) {
        active_ = true;
        contact_ = contact;
        started_ = now;
        pushed_ = 0;
    }
    pushed_ += distance;
    last_ = now;
    if (now - started_ >= config_.time && pushed_ >= config_.distance) {
        active_ = false;
        return true;
    }
    return false;
}

double EdgePush::progress(TimePoint now) const {
    if (!active_ || now - last_ > config_.gap) {
        return 0;
    }
    const double time = config_.time.count() <= 0 ? 1.0
                                                  : std::chrono::duration<double>(now - started_) /
                                                        std::chrono::duration<double>(config_.time);
    const double distance = config_.distance <= 0 ? 1.0
                                                  : static_cast<double>(pushed_) /
                                                        static_cast<double>(config_.distance);
    return std::clamp(std::min(time, distance), 0.0, 1.0);
}

} // namespace osdrop::layout
