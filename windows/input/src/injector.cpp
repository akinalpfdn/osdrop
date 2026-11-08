#include "injector.hpp"

#include <osdrop/win/absolute.hpp>

#include <array>
#include <cstdint>

namespace osdrop::input {
namespace {

namespace protocol = win::input;

// INPUT is Win32's tagged union; type says which member is set.
// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)

void send_mouse(DWORD flags, LONG dx = 0, LONG dy = 0, DWORD data = 0) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.mouseData = data;
    input.mi.dwFlags = flags;
    input.mi.dwExtraInfo = kInjectedTag;
    SendInput(1, &input, sizeof(INPUT));
}

void send_key(win::ScanCode scan, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = scan.code;
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (scan.extended ? KEYEVENTF_EXTENDEDKEY : 0U) |
                       (down ? 0U : KEYEVENTF_KEYUP);
    input.ki.dwExtraInfo = kInjectedTag;
    SendInput(1, &input, sizeof(INPUT));
}

// NOLINTEND(cppcoreguidelines-pro-type-union-access)

win::VirtualDesktop virtual_desktop() {
    return {.left = GetSystemMetrics(SM_XVIRTUALSCREEN),
            .top = GetSystemMetrics(SM_YVIRTUALSCREEN),
            .width = GetSystemMetrics(SM_CXVIRTUALSCREEN),
            .height = GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}

struct ButtonFlags {
    DWORD down = 0;
    DWORD up = 0;
    DWORD data = 0;
};

ButtonFlags flags_of(protocol::Button button) {
    switch (button) {
    case protocol::Button::Left:
        return {.down = MOUSEEVENTF_LEFTDOWN, .up = MOUSEEVENTF_LEFTUP, .data = 0};
    case protocol::Button::Right:
        return {.down = MOUSEEVENTF_RIGHTDOWN, .up = MOUSEEVENTF_RIGHTUP, .data = 0};
    case protocol::Button::Middle:
        return {.down = MOUSEEVENTF_MIDDLEDOWN, .up = MOUSEEVENTF_MIDDLEUP, .data = 0};
    case protocol::Button::X1:
        return {.down = MOUSEEVENTF_XDOWN, .up = MOUSEEVENTF_XUP, .data = XBUTTON1};
    case protocol::Button::X2:
        return {.down = MOUSEEVENTF_XDOWN, .up = MOUSEEVENTF_XUP, .data = XBUTTON2};
    }
    return {};
}

} // namespace

Injector::Injector(Hooks& hooks, CursorHider& cursors) : hooks_(hooks), cursors_(cursors) {}

void Injector::move(std::int32_t x, std::int32_t y) {
    const win::Normalized value = win::normalized(virtual_desktop(), x, y);
    send_mouse(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK, value.x, value.y);
}

void Injector::press(protocol::Button button, bool down) {
    const ButtonFlags flags = flags_of(button);
    send_mouse(down ? flags.down : flags.up, 0, 0, flags.data);
}

void Injector::scroll(std::int32_t vertical, std::int32_t horizontal) {
    // mouseData carries the signed delta in a DWORD.
    if (vertical != 0) {
        send_mouse(MOUSEEVENTF_WHEEL, 0, 0, static_cast<DWORD>(vertical));
    }
    if (horizontal != 0) {
        send_mouse(MOUSEEVENTF_HWHEEL, 0, 0, static_cast<DWORD>(horizontal));
    }
}

void Injector::key(win::ScanCode scan, bool down) {
    send_key(scan, down);
}

void Injector::start_capture(std::int32_t park_x, std::int32_t park_y) {
    const POINT park{.x = park_x, .y = park_y};
    hooks_.capture(park);
    SetCursorPos(park.x, park.y);
    cursors_.hide();
}

void Injector::stop_capture(std::int32_t x, std::int32_t y) {
    hooks_.release();
    SetCursorPos(x, y);
    cursors_.restore();
}

void Injector::set_controlled(bool controlled) {
    hooks_.set_controlled(controlled);
}

protocol::Stats Injector::stats() const {
    return hooks_.stats();
}

} // namespace osdrop::input
