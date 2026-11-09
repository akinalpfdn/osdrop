#pragma once

// Input another computer sends, as requests to OsdropInput (DEVPLAN Phase 3). Positions arrive in
// 1/16 of a native unit, which on Windows is a physical pixel; buttons and scrolls carry the
// position they happen at, so the cursor goes there first. Keys travel as HID usages and are
// injected by scan code, so this PC's layout makes the characters.

#include <osdrop/osdrop.h>
#include <osdrop/win/input_protocol.hpp>
#include <osdrop/win/wheel.hpp>

#include <vector>

namespace osdrop::win {

class Injection {
public:
    /// The requests for one input; `caps_lock_on` is Caps Lock here now, for CAPS_LOCK inputs.
    /// Nothing for inputs this PC has no way to inject (a key without a scan code, button 5 on).
    [[nodiscard]] std::vector<input::Request> requests(const osdrop_input& input,
                                                       bool caps_lock_on);

    /// A native position in 1/16 pixel, as a pixel (rounded down).
    [[nodiscard]] static std::int32_t pixel(std::int32_t sixteenths);

private:
    WheelFromScroll wheel_;
};

} // namespace osdrop::win
