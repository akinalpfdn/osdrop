#include "input/input_bridge.hpp"

#include <chrono>
#include <cstdlib>
#include <utility>
#include <variant>

namespace osdrop::engine {
namespace {

namespace protocol = win::input;

/// osdrop_mesh_set_unavailable: 0 available, 3 it may not inject input.
constexpr std::uint8_t kAvailable = 0;
constexpr std::uint8_t kCannotInject = 3;
constexpr auto kReconnectInterval = std::chrono::seconds(2);
/// While controlled, how often the cursor is checked for moves by something else.
constexpr auto kCursorCheckInterval = std::chrono::milliseconds(100);
/// Farther than this from where Osdrop put it, the cursor was moved by something else.
constexpr LONG kCursorReportDistance = 2;
constexpr std::int32_t kSixteenths = 16;

bool caps_lock_on() {
    return (GetKeyState(VK_CAPITAL) & 1) != 0;
}

} // namespace

InputBridge::InputBridge(win::GroupEngine& group) : group_(group), thread_([this] { run(); }) {}

InputBridge::~InputBridge() {
    stop();
}

void InputBridge::controlled(bool controlled) {
    const std::scoped_lock lock(mutex_);
    control_change_ = controlled;
    if (!controlled) {
        pending_.clear(); // nothing more for a control that ended
    }
    wake_.notify_one();
}

void InputBridge::inject(const osdrop_input& input) {
    const std::scoped_lock lock(mutex_);
    if (stopping_) {
        return;
    }
    pending_.push_back(input);
    wake_.notify_one();
}

void InputBridge::stop() {
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
        wake_.notify_one();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void InputBridge::run() {
    connect(); // at once: other computers learn whether this PC can be controlled
    while (true) {
        const Work work = take_work();
        if (work.stopping) {
            break;
        }
        if (!client_ || closed_) {
            connect();
        }
        apply(work);
        watch_cursor();
    }
    client_.reset(); // OsdropInput ends the control and releases what was pressed
}

InputBridge::Work InputBridge::take_work() {
    std::unique_lock lock(mutex_);
    const auto wait =
        controlled_ ? std::chrono::duration_cast<std::chrono::milliseconds>(kCursorCheckInterval)
                    : std::chrono::duration_cast<std::chrono::milliseconds>(kReconnectInterval);
    wake_.wait_for(lock, wait, [this] {
        return stopping_ || !pending_.empty() || control_change_.has_value();
    });
    Work work{.inputs = {pending_.begin(), pending_.end()},
              .control = std::exchange(control_change_, std::nullopt),
              .stopping = stopping_};
    pending_.clear();
    return work;
}

void InputBridge::connect() {
    client_.reset();
    closed_ = false;
    client_ = sys::InputClient::connect(
        [this](const protocol::Report& report) {
            if (std::holds_alternative<protocol::LocalInput>(report)) {
                local_input();
            }
        },
        [this] {
            closed_ = true;
            const std::scoped_lock lock(mutex_);
            wake_.notify_one();
        });
    const bool reachable = client_ != nullptr;
    if (reachable_ != reachable) {
        reachable_ = reachable;
        group_.set_unavailable(reachable ? kAvailable : kCannotInject);
    }
    if (reachable && controlled_) {
        static_cast<void>(client_->send(protocol::BeginControl{}));
    }
}

void InputBridge::apply(const Work& work) {
    if (work.control && *work.control != controlled_) {
        controlled_ = *work.control;
        moved_here_.reset();
        if (client_) {
            static_cast<void>(client_->send(controlled_
                                                ? protocol::Request(protocol::BeginControl{})
                                                : protocol::Request(protocol::EndControl{})));
        }
    }
    if (client_ && controlled_) {
        send_all(work.inputs);
    }
}

void InputBridge::send_all(const std::vector<osdrop_input>& inputs) {
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const bool superseded = inputs[i].kind == OSDROP_INPUT_POINTER && i + 1 < inputs.size() &&
                                inputs[i + 1].kind == OSDROP_INPUT_POINTER;
        if (superseded) {
            continue; // only the newest of a run of moves matters
        }
        for (const protocol::Request& request : injection_.requests(inputs[i], caps_lock_on())) {
            if (const auto* move = std::get_if<protocol::Move>(&request)) {
                expected_ = POINT{.x = move->x, .y = move->y};
            }
            if (!client_->send(request)) {
                return; // the pipe broke; the next round connects again
            }
        }
    }
}

void InputBridge::watch_cursor() {
    if (!controlled_ || !client_) {
        return;
    }
    POINT actual{};
    if (GetCursorPos(&actual) == FALSE) {
        return;
    }
    const bool moved = std::abs(actual.x - expected_.x) > kCursorReportDistance ||
                       std::abs(actual.y - expected_.y) > kCursorReportDistance;
    if (!moved) {
        moved_here_.reset();
        return;
    }
    // A position must hold for two checks: injected movement lands a little later.
    if (moved_here_ && moved_here_->x == actual.x && moved_here_->y == actual.y) {
        group_.cursor_moved(actual.x * kSixteenths, actual.y * kSixteenths);
        expected_ = actual;
        moved_here_.reset();
        return;
    }
    moved_here_ = actual;
}

void InputBridge::local_input() {
    POINT cursor{};
    GetCursorPos(&cursor);
    const osdrop_input input{.kind = OSDROP_INPUT_POINTER,
                             .x = cursor.x * kSixteenths,
                             .y = cursor.y * kSixteenths,
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
    group_.physical_input(input); // core gives the control back to this PC's user
}

} // namespace osdrop::engine
