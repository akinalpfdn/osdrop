#include <osdrop/win/forwarder.hpp>
#include <osdrop/win/key_codes.hpp>
#include <osdrop/win/wheel.hpp>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <utility>
#include <variant>

namespace osdrop::win {
namespace {

constexpr std::int32_t kSixteenths = 16;
constexpr std::uint8_t kDown = 1;
constexpr std::uint8_t kUp = 2;
constexpr std::uint8_t kRepeat = 3;
constexpr std::uint8_t kLines = 1;

osdrop_input blank(osdrop_input_kind kind) {
    return osdrop_input{.kind = kind,
                        .x = 0,
                        .y = 0,
                        .dx = 0,
                        .dy = 0,
                        .page = 0,
                        .usage = 0,
                        .action = 0,
                        .button = 0,
                        .click_count = 0,
                        .scroll_unit = 0,
                        .scroll_phase = 0,
                        .momentum = 0};
}

std::int32_t added(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        std::int64_t{a} + b, -input::kMaxCoordinate, input::kMaxCoordinate));
}

std::int64_t squared(std::int64_t value) {
    return value * value;
}

} // namespace

InputForwarder::InputForwarder(ClickSettings clicks) : clicks_(clicks) {}

void InputForwarder::set_monitors(std::vector<PhysicalMonitor> monitors) {
    monitors_ = std::move(monitors);
}

void InputForwarder::capture(Pixel park) {
    captured_ = true;
    position_ = park;
    travelled_ = {};
    last_press_.reset();
}

void InputForwarder::release(Pixel at) {
    captured_ = false;
    position_ = at;
    last_press_.reset();
}

void InputForwarder::reset() {
    keys_down_.clear();
    last_press_.reset();
    press_counts_ = {};
}

std::optional<Forwarded> InputForwarder::convert(const input::Report& report,
                                                 Clock::time_point now) {
    if (const auto* motion = std::get_if<input::WatchedMotion>(&report)) {
        position_ = on_monitor({.x = motion->x, .y = motion->y});
        return Forwarded{.input = pointer(motion->dx, motion->dy), .ticks = motion->ticks};
    }
    if (const auto* motion = std::get_if<input::CapturedMotion>(&report)) {
        travelled_ = {.x = added(travelled_.x, motion->dx), .y = added(travelled_.y, motion->dy)};
        return Forwarded{.input = pointer(motion->dx, motion->dy), .ticks = motion->ticks};
    }
    if (const auto* press = std::get_if<input::PhysicalPress>(&report)) {
        return Forwarded{.input = button(*press, now), .ticks = 0};
    }
    if (const auto* scrolled = std::get_if<input::PhysicalScroll>(&report)) {
        return Forwarded{.input = scroll(*scrolled), .ticks = 0};
    }
    if (const auto* pressed = std::get_if<input::PhysicalKey>(&report)) {
        if (auto event = key(*pressed)) {
            return Forwarded{.input = *event, .ticks = 0};
        }
    }
    return std::nullopt;
}

Pixel InputForwarder::on_monitor(Pixel point) const {
    std::optional<Pixel> nearest;
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    for (const PhysicalMonitor& monitor : monitors_) {
        if (monitor.right <= monitor.left || monitor.bottom <= monitor.top) {
            continue;
        }
        const Pixel inside{.x = std::clamp(point.x, monitor.left, monitor.right - 1),
                           .y = std::clamp(point.y, monitor.top, monitor.bottom - 1)};
        const std::int64_t distance =
            squared(std::int64_t{point.x} - inside.x) + squared(std::int64_t{point.y} - inside.y);
        if (distance < best) {
            best = distance;
            nearest = inside;
        }
    }
    return nearest.value_or(point);
}

osdrop_input InputForwarder::pointer(std::int32_t dx, std::int32_t dy) const {
    osdrop_input event = blank(OSDROP_INPUT_POINTER);
    event.x = position_.x * kSixteenths;
    event.y = position_.y * kSixteenths;
    event.dx = dx * kSixteenths;
    event.dy = dy * kSixteenths;
    return event;
}

osdrop_input InputForwarder::button(const input::PhysicalPress& press, Clock::time_point now) {
    const auto index = static_cast<std::size_t>(press.button);
    std::uint8_t count = 1;
    if (press.down) {
        // While captured the cursor waits in one place; the movement since says where it is.
        const Pixel at = captured_ ? travelled_ : position_;
        const bool continues =
            last_press_ && last_press_->button == press.button &&
            now - last_press_->time <= clicks_.interval &&
            std::abs(std::int64_t{at.x} - last_press_->at.x) <= clicks_.width / 2 &&
            std::abs(std::int64_t{at.y} - last_press_->at.y) <= clicks_.height / 2;
        count = continues && last_press_->count < UINT8_MAX
                    ? static_cast<std::uint8_t>(last_press_->count + 1)
                    : std::uint8_t{1};
        last_press_ = LastPress{.button = press.button, .at = at, .time = now, .count = count};
        press_counts_.at(index) = count;
    } else {
        count = std::max(press_counts_.at(index), std::uint8_t{1});
    }
    osdrop_input event = blank(OSDROP_INPUT_BUTTON);
    event.x = position_.x * kSixteenths;
    event.y = position_.y * kSixteenths;
    event.action = press.down ? kDown : kUp;
    event.button = static_cast<std::uint8_t>(press.button);
    event.click_count = count;
    return event;
}

std::optional<osdrop_input> InputForwarder::key(const input::PhysicalKey& pressed) {
    const auto usage = usage_of(pressed.scan);
    if (!usage) {
        return std::nullopt;
    }
    std::uint8_t action = kUp;
    if (pressed.down) {
        action = keys_down_.insert(pressed.scan).second ? kDown : kRepeat;
    } else {
        keys_down_.erase(pressed.scan);
    }
    osdrop_input event = blank(OSDROP_INPUT_KEY);
    event.page = usage->page;
    event.usage = usage->usage;
    event.action = action;
    return event;
}

osdrop_input InputForwarder::scroll(const input::PhysicalScroll& scrolled) const {
    const LineScroll lines =
        scroll_of(Wheel{.vertical = scrolled.vertical, .horizontal = scrolled.horizontal});
    osdrop_input event = blank(OSDROP_INPUT_SCROLL);
    event.x = position_.x * kSixteenths;
    event.y = position_.y * kSixteenths;
    event.dx = lines.dx;
    event.dy = lines.dy;
    event.scroll_unit = kLines;
    return event;
}

} // namespace osdrop::win
