#include <osdrop/win/input_session.hpp>

#include <string>
#include <utility>

namespace osdrop::win::input {
namespace {

template <typename... Visitors>
struct Overloaded : Visitors... { // NOLINT(misc-multiple-inheritance): the standard visitor idiom
    using Visitors::operator()...;
};

bool injects(const Request& request) {
    return std::holds_alternative<Move>(request) || std::holds_alternative<Press>(request) ||
           std::holds_alternative<Scroll>(request) || std::holds_alternative<Key>(request);
}

template <typename T>
void track(std::set<T>& held, T value, bool down) {
    if (down) {
        held.insert(value);
    } else {
        held.erase(value);
    }
}

std::string_view describe(DecodeError error) {
    switch (error) {
    case DecodeError::Truncated:
        return "truncated";
    case DecodeError::BadMagic:
        return "not an Osdrop input message";
    case DecodeError::UnsupportedVersion:
        return "another protocol version";
    case DecodeError::UnknownType:
        return "unknown type";
    case DecodeError::LengthMismatch:
        return "wrong length";
    case DecodeError::ValueOutOfRange:
        return "a value out of range";
    }
    return "invalid";
}

} // namespace

InputSession::InputSession(InputDevice& device, InputLog& log) : device_(device), log_(log) {}

std::optional<Report> InputSession::handle(std::span<const std::uint8_t> message,
                                           Clock::time_point now) {
    const std::uint16_t type = type_of(message);
    const auto request = decode_request(message);
    if (!request) {
        const Refusal reason = request.error() == DecodeError::UnsupportedVersion
                                   ? Refusal::UnsupportedVersion
                                   : Refusal::Malformed;
        return refuse(type, reason, describe(request.error()));
    }
    return carry_out(request.value(), now);
}

std::optional<Report> InputSession::carry_out(const Request& request, Clock::time_point now) {
    if (injects(request)) {
        // Capturing injects too: keys whose release now goes elsewhere, and Caps Lock.
        if (!controlling_ && !capture_) {
            return refuse(type_of(request), Refusal::NotControlling,
                          "injection without control or capture");
        }
        inject(request);
        return std::nullopt;
    }
    return std::visit(Overloaded{
                          [&](const BeginControl&) { return set_control(true); },
                          [&](const EndControl&) { return set_control(false); },
                          [&](const StartCapture& m) { return start_capture(m, now); },
                          [&](const StopCapture& m) { return stop_capture(m); },
                          [&](const Heartbeat&) { return heartbeat(now); },
                          [&](const Watch& m) { return watch(m.on); },
                          [&](const SetChords& m) { return set_chords(m); },
                          [&](const HoldDrag&) { return hold_drag(); },
                          [&](const ReleaseDrag&) { return release_drag(); },
                          [&](const PressForDrag& m) { return press_for_drag(m); },
                          [&](const QueryStats&) { return std::optional<Report>(device_.stats()); },
                          [](const auto&) { return std::optional<Report>(); }, // injection
                      },
                      request);
}

void InputSession::inject(const Request& request) {
    std::visit(Overloaded{
                   [&](const Move& m) { device_.move(m.x, m.y); },
                   [&](const Press& m) {
                       track(buttons_down_, m.button, m.down);
                       device_.press(m.button, m.down);
                   },
                   [&](const Scroll& m) { device_.scroll(m.vertical, m.horizontal); },
                   [&](const Key& m) {
                       track(keys_down_, m.scan, m.down);
                       device_.key(m.scan, m.down);
                   },
                   [](const auto&) {},
               },
               request);
}

std::optional<Report> InputSession::set_control(bool controlling) {
    if (controlling_ == controlling) {
        return std::nullopt;
    }
    if (!controlling) {
        release_all();
    }
    controlling_ = controlling;
    device_.set_controlled(controlling);
    log_.info(controlling ? "Control began" : "Control ended");
    return std::nullopt;
}

std::optional<Report> InputSession::start_capture(const StartCapture& start,
                                                  Clock::time_point now) {
    capture_ = Capture{.home_x = start.home_x,
                       .home_y = start.home_y,
                       .deadline = now + kHeartbeatTimeout,
                       .released = false};
    device_.start_capture(start.park_x, start.park_y);
    return std::nullopt;
}

std::optional<Report> InputSession::stop_capture(const StopCapture& stop) {
    if (!capture_) {
        return std::nullopt;
    }
    capture_.reset();
    device_.stop_capture(stop.x, stop.y);
    if (!controlling_) {
        release_all(); // what the capture pressed here
    }
    return std::nullopt;
}

std::optional<Report> InputSession::heartbeat(Clock::time_point now) {
    if (capture_ && !capture_->released) {
        capture_->deadline = now + kHeartbeatTimeout;
    }
    return std::nullopt; // a late one, after the capture ended here, is no error
}

std::optional<Report> InputSession::set_chords(const SetChords& chords) {
    if (chords != chords_) {
        chords_ = chords;
        device_.set_chords(chords.return_home, chords.lock_input);
        log_.info("The client set its chords");
    }
    return std::nullopt;
}

std::optional<Report> InputSession::hold_drag() {
    if (buttons_down_.erase(Button::Left) == 1) {
        drag_held_ = true; // EndControl leaves it down
        log_.info("The left button stays down for a drag");
    }
    return std::nullopt;
}

std::optional<Report> InputSession::release_drag() {
    // Any state: a drag held past its control, or one this PC's own hand started, which waits
    // at the edge while the input is captured or after the control moved on.
    buttons_down_.erase(Button::Left);
    drag_held_ = false;
    drag_pressed_ = false;
    device_.press(Button::Left, false);
    log_.info("Released the left button of a drag");
    return std::nullopt;
}

std::optional<Report> InputSession::press_for_drag(const PressForDrag& press) {
    // The hand that holds the button lets it go: no state of the client's has to release it.
    if (!device_.press_held_left()) {
        return refuse(type_of(Request(press)), Refusal::NotHeld,
                      "a drag's press while this PC's left button is up");
    }
    drag_pressed_ = true;
    log_.info("Pressed the left button for a drag that came home");
    return std::nullopt;
}

std::optional<Report> InputSession::watch(bool on) {
    if (watching_ != on) {
        watching_ = on;
        device_.set_watching(on);
        log_.info(on ? "Watching input" : "Not watching input");
    }
    return std::nullopt;
}

void InputSession::forwarding(const Report& report) {
    if (const auto* released = std::get_if<Released>(&report)) {
        // The hooks already let input through; the cursor shows where it left.
        if (released->reason == ReleaseReason::ReturnChord && end_capture_here()) {
            log_.info("Capture ended by the return chord");
        }
    } else if (std::holds_alternative<HotkeyPressed>(report) && !capturing()) {
        device_.mask_menu_keys(); // its modifiers reached the apps; the key did not
    } else if (const auto* press = std::get_if<PhysicalPress>(&report);
               press != nullptr && press->button == Button::Left && !press->down) {
        drag_pressed_ = false; // the release reached the apps and ended the drag's press
    }
}

std::optional<InputSession::Clock::time_point> InputSession::deadline() const {
    if (capture_ && !capture_->released) {
        return capture_->deadline;
    }
    return std::nullopt;
}

std::optional<Report> InputSession::expire(Clock::time_point now) {
    if (!capture_ || capture_->released || now < capture_->deadline) {
        return std::nullopt;
    }
    log_.warning("Capture ended: the client stopped confirming it");
    end_capture_here();
    return Released{.reason = ReleaseReason::Watchdog};
}

bool InputSession::end_capture_here() {
    if (!capture_ || capture_->released) {
        return false;
    }
    capture_->released = true;
    device_.stop_capture(capture_->home_x, capture_->home_y);
    return true;
}

std::optional<Report> InputSession::refuse(std::uint16_t type, Refusal reason,
                                           std::string_view why) {
    log_.warning("Rejected " + std::string(type_name(type)) + " (" + std::to_string(type) +
                 "): " + std::string(why));
    return Rejected{.type = type, .reason = reason};
}

void InputSession::reset() {
    end_capture_here();
    capture_.reset();
    release_all();
    const bool held = std::exchange(drag_held_, false);
    const bool pressed = std::exchange(drag_pressed_, false);
    if (held || pressed) {
        device_.press(Button::Left, false);
    }
    if (controlling_) {
        controlling_ = false;
        device_.set_controlled(false);
    }
    if (watching_) {
        watching_ = false;
        device_.set_watching(false);
    }
    if (chords_ != SetChords{}) {
        chords_ = SetChords{}; // the next client starts from the defaults
        device_.set_chords(chords_.return_home, chords_.lock_input);
    }
}

void InputSession::release_all() {
    // Copies: the device may report back synchronously in tests.
    const std::set<ScanCode> keys = std::exchange(keys_down_, {});
    const std::set<Button> buttons = std::exchange(buttons_down_, {});
    for (const ScanCode scan : keys) {
        device_.key(scan, false);
    }
    for (const Button button : buttons) {
        device_.press(button, false);
    }
}

} // namespace osdrop::win::input
