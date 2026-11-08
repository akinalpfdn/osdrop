#include <osdrop/win/input_session.hpp>

#include <string>
#include <utility>

namespace osdrop::win::input {
namespace {

template <typename... Visitors>
struct Overloaded : Visitors... { // NOLINT(misc-multiple-inheritance): the standard visitor idiom
    using Visitors::operator()...;
};

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

InputSession::~InputSession() {
    reset();
}

std::optional<Report> InputSession::handle(std::span<const std::uint8_t> message) {
    const std::uint16_t type = type_of(message);
    const auto request = decode_request(message);
    if (!request) {
        const Refusal reason = request.error() == DecodeError::UnsupportedVersion
                                   ? Refusal::UnsupportedVersion
                                   : Refusal::Malformed;
        return refuse(type, reason, describe(request.error()));
    }
    return carry_out(request.value());
}

std::optional<Report> InputSession::carry_out(const Request& request) {
    const auto injecting = [&]() -> std::optional<Report> {
        return controlling_
                   ? std::nullopt
                   : refuse(type_of(request), Refusal::NotControlling, "injection without control");
    };
    return std::visit(
        Overloaded{
            [&](const BeginControl&) -> std::optional<Report> {
                if (!controlling_) {
                    controlling_ = true;
                    device_.set_controlled(true);
                    log_.info("Control began");
                }
                return std::nullopt;
            },
            [&](const EndControl&) -> std::optional<Report> {
                if (controlling_) {
                    release_all();
                    controlling_ = false;
                    device_.set_controlled(false);
                    log_.info("Control ended");
                }
                return std::nullopt;
            },
            [&](const Move& m) -> std::optional<Report> {
                if (auto refused = injecting()) {
                    return refused;
                }
                device_.move(m.x, m.y);
                return std::nullopt;
            },
            [&](const Press& m) -> std::optional<Report> {
                if (auto refused = injecting()) {
                    return refused;
                }
                if (m.down) {
                    buttons_down_.insert(m.button);
                } else {
                    buttons_down_.erase(m.button);
                }
                device_.press(m.button, m.down);
                return std::nullopt;
            },
            [&](const Scroll& m) -> std::optional<Report> {
                if (auto refused = injecting()) {
                    return refused;
                }
                device_.scroll(m.vertical, m.horizontal);
                return std::nullopt;
            },
            [&](const Key& m) -> std::optional<Report> {
                if (auto refused = injecting()) {
                    return refused;
                }
                if (m.down) {
                    keys_down_.insert(m.scan);
                } else {
                    keys_down_.erase(m.scan);
                }
                device_.key(m.scan, m.down);
                return std::nullopt;
            },
            [&](const StartCapture& m) -> std::optional<Report> {
                capture_ = Park{.x = m.park_x, .y = m.park_y};
                device_.start_capture(m.park_x, m.park_y);
                return std::nullopt;
            },
            [&](const StopCapture& m) -> std::optional<Report> {
                if (capture_) {
                    capture_.reset();
                    device_.stop_capture(m.x, m.y);
                }
                return std::nullopt;
            },
            [&](const QueryStats&) -> std::optional<Report> { return device_.stats(); },
        },
        request);
}

std::optional<Report> InputSession::refuse(std::uint16_t type, Refusal reason,
                                           std::string_view why) {
    log_.warning("Rejected " + std::string(type_name(type)) + " (" + std::to_string(type) +
                 "): " + std::string(why));
    return Rejected{.type = type, .reason = reason};
}

void InputSession::reset() {
    if (const auto park = capture_) {
        capture_.reset();
        device_.stop_capture(park->x, park->y);
    }
    release_all();
    if (controlling_) {
        controlling_ = false;
        device_.set_controlled(false);
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
