#include "input/input_bridge.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace osdrop::engine {
namespace {

namespace protocol = win::input;

/// osdrop_mesh_set_unavailable: 0 available, 3 it may not inject input.
constexpr std::uint8_t kAvailable = 0;
constexpr std::uint8_t kCannotInject = 3;
constexpr auto kReconnectInterval = std::chrono::seconds(2);
/// While the bridge has timers running, how often its thread looks at them.
constexpr auto kTickInterval = std::chrono::milliseconds(100);
/// While controlled, how often the cursor is checked for moves by something else.
constexpr auto kCursorCheckInterval = std::chrono::milliseconds(100);
/// Farther than this from where Osdrop put it, the cursor was moved by something else.
constexpr LONG kCursorReportDistance = 2;
/// While watching, how often the rectangle a game may hold the cursor in is read.
constexpr auto kClipCheckInterval = std::chrono::milliseconds(100);
/// While the cursor is home, how often Caps Lock is read; after Osdrop sets it, how long before.
constexpr auto kCapsCheckInterval = std::chrono::milliseconds(250);
constexpr auto kCapsSettleTime = std::chrono::milliseconds(300);
constexpr std::int32_t kSixteenths = 16;
constexpr std::uint8_t kCapsOn = 1;
constexpr std::uint8_t kCapsOff = 2;
constexpr std::int64_t kMicrosPerSecond = 1'000'000;

bool caps_lock_on() {
    return (GetKeyState(VK_CAPITAL) & 1) != 0;
}

/// Physical pixels for what this thread asks while it lives, whatever the process declares.
class PhysicalPixels {
public:
    PhysicalPixels()
        : previous_(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
    ~PhysicalPixels() {
        if (previous_ != nullptr) {
            SetThreadDpiAwarenessContext(previous_);
        }
    }
    PhysicalPixels(const PhysicalPixels&) = delete;
    PhysicalPixels& operator=(const PhysicalPixels&) = delete;
    PhysicalPixels(PhysicalPixels&&) = delete;
    PhysicalPixels& operator=(PhysicalPixels&&) = delete;

private:
    DPI_AWARENESS_CONTEXT previous_;
};

POINT cursor_position() {
    const PhysicalPixels scope;
    POINT cursor{};
    GetCursorPos(&cursor);
    return cursor;
}

/// The middle of the monitor the cursor is on: where it waits, hidden, while captured, away from
/// edges and corners that Windows reacts to.
POINT park_point(POINT cursor) {
    const PhysicalPixels scope;
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &info) == FALSE) {
        return cursor;
    }
    return {.x = info.rcMonitor.left + ((info.rcMonitor.right - info.rcMonitor.left) / 2),
            .y = info.rcMonitor.top + ((info.rcMonitor.bottom - info.rcMonitor.top) / 2)};
}

LARGE_INTEGER performance_frequency() {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    return frequency;
}

win::ClickSettings click_settings() {
    return {.interval = std::chrono::milliseconds(GetDoubleClickTime()),
            .width = GetSystemMetrics(SM_CXDOUBLECLK),
            .height = GetSystemMetrics(SM_CYDOUBLECLK)};
}

osdrop_input caps_lock(bool on) {
    return osdrop_input{.kind = OSDROP_INPUT_CAPS_LOCK,
                        .x = 0,
                        .y = 0,
                        .dx = 0,
                        .dy = 0,
                        .page = 0,
                        .usage = 0,
                        .action = on ? kCapsOn : kCapsOff,
                        .button = 0,
                        .click_count = 0,
                        .scroll_unit = 0,
                        .scroll_phase = 0,
                        .momentum = 0};
}

template <typename... Visitors>
struct Overloaded : Visitors... { // NOLINT(misc-multiple-inheritance): the standard visitor idiom
    using Visitors::operator()...;
};

} // namespace

InputBridge::InputBridge(win::GroupEngine& group, Listener listener)
    : group_(group), listener_(std::move(listener)), frequency_(performance_frequency()),
      forwarder_(click_settings()), thread_([this] { run(); }) {}

InputBridge::~InputBridge() {
    stop();
}

void InputBridge::controlled(bool controlled) {
    const std::scoped_lock lock(mutex_);
    control_change_ = controlled; // core's releases before it go out first
    wake_.notify_one();
}

void InputBridge::release_drag() {
    const std::scoped_lock lock(mutex_);
    release_drag_ = true;
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

void InputBridge::press_for_drag(const osdrop_input& press) {
    const std::scoped_lock lock(mutex_);
    if (stopping_) {
        return;
    }
    pending_.push_back(press); // in order with the controller's input, while controlled
    drag_press_ = true;
    wake_.notify_one();
}

void InputBridge::capture_began(const std::vector<osdrop_input>& release_here) {
    capturing_ = true; // the reader thread's input is captured input from now on
    const std::scoped_lock lock(mutex_);
    capture_changes_.emplace_back(Began{.release_here = release_here});
    wake_.notify_one();
}

void InputBridge::capture_ended(const win::CaptureEnd& end) {
    const std::scoped_lock lock(mutex_);
    capture_changes_.emplace_back(end);
    wake_.notify_one();
}

void InputBridge::set_watching(bool watching) {
    const std::scoped_lock lock(mutex_);
    if (watching_wanted_ != watching) {
        watching_wanted_ = watching;
        watching_changed_ = true;
        wake_.notify_one();
    }
}

void InputBridge::set_chords(const protocol::Chord& return_home,
                             const protocol::Chord& lock_input) {
    const std::scoped_lock lock(mutex_);
    const Chords chords{.return_home = return_home, .lock_input = lock_input};
    if (chords_wanted_ != chords) {
        chords_wanted_ = chords;
        chords_changed_ = true;
        wake_.notify_one();
    }
}

void InputBridge::set_monitors(std::vector<win::PhysicalMonitor> monitors) {
    const std::scoped_lock lock(forwarder_mutex_);
    forwarder_.set_monitors(std::move(monitors));
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
    // Every position this thread reads and writes is in physical pixels, as OsdropInput's are.
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
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
        tick();
    }
    client_.reset(); // OsdropInput ends the control and the capture, and releases what was pressed
}

InputBridge::Work InputBridge::take_work() {
    std::unique_lock lock(mutex_);
    const bool timers = controlled_ || watching_ || capturing_;
    const auto wait =
        timers ? std::chrono::duration_cast<std::chrono::milliseconds>(kTickInterval)
               : std::chrono::duration_cast<std::chrono::milliseconds>(kReconnectInterval);
    wake_.wait_for(lock, wait, [this] {
        return stopping_ || !pending_.empty() || control_change_.has_value() ||
               !capture_changes_.empty() || watching_changed_ || chords_changed_ || release_drag_;
    });
    watching_changed_ = false;
    chords_changed_ = false;
    Work work{.inputs = {pending_.begin(), pending_.end()},
              .control = std::exchange(control_change_, std::nullopt),
              .captures = std::exchange(capture_changes_, {}),
              .watching = watching_wanted_,
              .chords = chords_wanted_,
              .release_drag = std::exchange(release_drag_, false),
              .drag_press = std::exchange(drag_press_, false),
              .stopping = stopping_};
    pending_.clear();
    return work;
}

void InputBridge::connect() {
    client_.reset();
    closed_ = false;
    watching_ = false;    // a new OsdropInput watches nothing yet
    chords_sent_.reset(); // and has the default chords
    left_down_ = false;   // and released what the old one pressed
    {
        const std::scoped_lock lock(forwarder_mutex_);
        forwarder_.reset();
    }
    client_ = sys::InputClient::connect(
        [this](const protocol::Report& report) { received(report); },
        [this] {
            closed_ = true;
            if (capturing_) {
                group_.return_home(true); // the capture is gone with OsdropInput
            }
            const std::scoped_lock lock(mutex_);
            wake_.notify_one();
        });
    const bool reachable = client_ != nullptr;
    if (reachable_ != reachable) {
        reachable_ = reachable;
        group_.set_unavailable(reachable ? kAvailable : kCannotInject);
        if (listener_.reachable) {
            listener_.reachable(reachable);
        }
    }
    if (reachable && controlled_) {
        static_cast<void>(client_->send(protocol::BeginControl{}));
    }
}

void InputBridge::apply(const Work& work) {
    send_chords(work.chords);
    watch(work.watching);
    for (const CaptureChange& change : work.captures) {
        std::visit(Overloaded{[this](const Began& began) { begin_capture(began); },
                              [this](const win::CaptureEnd& end) { end_capture(end); }},
                   change);
    }
    const bool ending = work.control && !*work.control && controlled_;
    if (ending && client_) {
        send_all(work.inputs); // core's releases, while injection is still allowed
        if (left_down_) {
            // Core left it down: this PC's drag waits at the edge for its outcome.
            static_cast<void>(client_->send(protocol::HoldDrag{}));
        }
    }
    if (work.control && *work.control != controlled_) {
        controlled_ = *work.control;
        moved_here_.reset();
        if (client_) {
            static_cast<void>(client_->send(controlled_
                                                ? protocol::Request(protocol::BeginControl{})
                                                : protocol::Request(protocol::EndControl{})));
        }
    }
    if (client_ && controlled_ && !ending) {
        send_all(work.inputs);
    } else if (!controlled_ && work.drag_press) {
        // The drag came home with this PC's own hand, whose press went to the other computer.
        if ((!client_ || !client_->send(protocol::PressForDrag{})) && listener_.drag_unpressed) {
            listener_.drag_unpressed();
        }
    }
    if (work.release_drag && client_) {
        left_down_ = false;
        static_cast<void>(client_->send(protocol::ReleaseDrag{}));
    }
}

void InputBridge::watch(bool watching) {
    if (client_ && watching != watching_) {
        watching_ = watching;
        static_cast<void>(client_->send(protocol::Watch{.on = watching}));
    }
}

void InputBridge::send_chords(const Chords& chords) {
    if (client_ && chords_sent_ != chords && (chords_sent_ || chords != Chords{})) {
        chords_sent_ = chords;
        static_cast<void>(client_->send(chords));
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
            if (const auto* press = std::get_if<protocol::Press>(&request);
                press != nullptr && press->button == protocol::Button::Left) {
                left_down_ = press->down;
            }
            if (!client_->send(request)) {
                return; // the pipe broke; the next round connects again
            }
        }
    }
}

void InputBridge::begin_capture(const Began& began) {
    const POINT home = cursor_position();
    const POINT park = park_point(home);
    if (listener_.parking) {
        listener_.parking(park); // a drag of this PC carried away waits there
    }
    {
        const std::scoped_lock lock(forwarder_mutex_);
        forwarder_.capture({.x = park.x, .y = park.y});
    }
    capturing_ = true;
    last_heartbeat_ = Clock::now();
    if (!client_ || !client_->send(protocol::StartCapture{
                        .park_x = park.x, .park_y = park.y, .home_x = home.x, .home_y = home.y})) {
        group_.return_home(true); // nothing can capture this PC's input
        return;
    }
    // Their release now goes to the other computer: apps here see it now.
    for (const osdrop_input& key : began.release_here) {
        for (const protocol::Request& request : injection_.requests(key, caps_lock_on())) {
            static_cast<void>(client_->send(request));
        }
    }
}

void InputBridge::end_capture(const win::CaptureEnd& end) {
    const POINT at{.x = win::Injection::pixel(end.x), .y = win::Injection::pixel(end.y)};
    if (client_) {
        // Caps Lock as the user left it on the other computers, while injection is still allowed.
        for (const protocol::Request& request :
             injection_.requests(caps_lock(end.caps_lock), caps_lock_on())) {
            static_cast<void>(client_->send(request));
        }
        static_cast<void>(client_->send(protocol::StopCapture{.x = at.x, .y = at.y}));
    }
    {
        const std::scoped_lock lock(forwarder_mutex_);
        forwarder_.release({.x = at.x, .y = at.y});
    }
    capturing_ = false;
    caps_reported_ = end.caps_lock;
    caps_settled_ = Clock::now() + kCapsSettleTime;
}

void InputBridge::tick() {
    const Clock::time_point now = Clock::now();
    if (capturing_ && client_ && now - last_heartbeat_ >= protocol::kHeartbeatInterval) {
        last_heartbeat_ = now;
        static_cast<void>(client_->send(protocol::Heartbeat{}));
    }
    if (controlled_ && client_ && now - last_cursor_check_ >= kCursorCheckInterval) {
        last_cursor_check_ = now;
        watch_cursor();
    }
    if (watching_ && !capturing_ && now >= caps_settled_ &&
        now - last_caps_check_ >= kCapsCheckInterval) {
        last_caps_check_ = now;
        report_caps_lock();
    }
    if (watching_ && !capturing_ && now - last_clip_check_ >= kClipCheckInterval) {
        last_clip_check_ = now;
        check_clip();
    }
}

void InputBridge::set_respects_clip(bool respects) {
    respects_clip_ = respects; // the next check applies it
}

void InputBridge::check_clip() {
    // A game that keeps the cursor on its monitor holds it in a rectangle smaller than the
    // desktop; then it does not cross to another computer either.
    std::optional<win::PixelRect> clip;
    RECT held{};
    if (respects_clip_ && GetClipCursor(&held) != FALSE) {
        const RECT desktop{
            .left = GetSystemMetrics(SM_XVIRTUALSCREEN),
            .top = GetSystemMetrics(SM_YVIRTUALSCREEN),
            .right = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
            .bottom = GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
        const bool smaller = held.left > desktop.left || held.top > desktop.top ||
                             held.right < desktop.right || held.bottom < desktop.bottom;
        if (smaller) {
            clip = win::PixelRect{
                .left = held.left, .top = held.top, .right = held.right, .bottom = held.bottom};
        }
    }
    if (clip == clip_) {
        return;
    }
    if (clip.has_value() != clip_.has_value() && listener_.log) {
        listener_.log(clip ? "An application holds the cursor: no crossings"
                           : "The cursor is free again: crossings resume");
    }
    clip_ = clip;
    const std::scoped_lock lock(forwarder_mutex_);
    forwarder_.set_clip(clip);
}

void InputBridge::watch_cursor() {
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

void InputBridge::report_caps_lock() {
    // While the cursor is away, core follows the Caps Lock presses it sends.
    const bool on = caps_lock_on();
    if (caps_reported_ != on) {
        caps_reported_ = on;
        group_.set_caps_lock(on);
    }
}

// --- The pipe's reader thread --------------------------------------------------------------------

void InputBridge::received(const protocol::Report& report) {
    if (std::holds_alternative<protocol::LocalInput>(report)) {
        local_input();
        return;
    }
    if (const auto* pressed = std::get_if<protocol::HotkeyPressed>(&report)) {
        hotkey(pressed->hotkey);
        return;
    }
    if (const auto* released = std::get_if<protocol::Released>(&report)) {
        // OsdropInput gave the input back already; core brings the cursor home.
        group_.return_home(released->reason == protocol::ReleaseReason::Watchdog);
        return;
    }
    if (const auto* rejected = std::get_if<protocol::Rejected>(&report);
        rejected != nullptr && rejected->reason == protocol::Refusal::NotHeld) {
        if (listener_.log) {
            listener_.log("A drag that came home was let go before it could start here");
        }
        if (listener_.drag_unpressed) {
            listener_.drag_unpressed();
        }
        return;
    }
    std::optional<win::Forwarded> forwarded;
    {
        const std::scoped_lock lock(forwarder_mutex_);
        forwarded = forwarder_.convert(report, Clock::now());
    }
    if (forwarded) {
        group_.physical_input(forwarded->input, age_us(forwarded->ticks));
    }
}

void InputBridge::local_input() {
    const POINT cursor = cursor_position();
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
    group_.physical_input(input, 0); // core gives the control back to this PC's user
}

void InputBridge::hotkey(protocol::Hotkey hotkey) {
    switch (hotkey) {
    case protocol::Hotkey::ReturnHome:
        group_.return_home(false);
        break;
    case protocol::Hotkey::LockInput:
        set_locked(!locked_);
        break;
    }
}

void InputBridge::set_locked(bool locked) {
    if (locked_.exchange(locked) == locked) {
        return;
    }
    group_.lock_input(locked);
    if (listener_.locked) {
        listener_.locked(locked);
    }
}

std::uint64_t InputBridge::age_us(std::uint64_t ticks) const {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const auto current = static_cast<std::uint64_t>(now.QuadPart);
    if (ticks == 0 || frequency_.QuadPart <= 0 || current <= ticks) {
        return 0;
    }
    return (current - ticks) * kMicrosPerSecond / static_cast<std::uint64_t>(frequency_.QuadPart);
}

} // namespace osdrop::engine
