#include <osdrop/win/injection.hpp>
#include <osdrop/win/key_codes.hpp>

#include <algorithm>

namespace osdrop::win {
namespace {

constexpr ScanCode kCapsLock{.code = 0x3A, .extended = false};
constexpr std::uint8_t kDown = 1;
constexpr std::uint8_t kUp = 2;
constexpr std::uint8_t kRepeat = 3;

input::Move move_to(const osdrop_input& input) {
    return {.x = Injection::pixel(input.x), .y = Injection::pixel(input.y)};
}

} // namespace

std::int32_t Injection::pixel(std::int32_t sixteenths) {
    // Arithmetic shift rounds down for negative positions too (monitors left of the main one).
    const std::int32_t value = sixteenths >> 4; // NOLINT(hicpp-signed-bitwise)
    return std::clamp(value, -input::kMaxCoordinate, input::kMaxCoordinate);
}

std::vector<input::Request> Injection::requests(const osdrop_input& input, bool caps_lock_on) {
    switch (input.kind) {
    case OSDROP_INPUT_POINTER:
        return {move_to(input)};
    case OSDROP_INPUT_BUTTON:
        if (input.button > static_cast<std::uint8_t>(input::Button::X2) ||
            (input.action != kDown && input.action != kUp)) {
            return {};
        }
        return {move_to(input), input::Press{.button = static_cast<input::Button>(input.button),
                                             .down = input.action == kDown}};
    case OSDROP_INPUT_KEY: {
        const auto scan = scan_code_of(HidUsage{.page = input.page, .usage = input.usage});
        if (!scan || (input.action != kDown && input.action != kUp && input.action != kRepeat)) {
            return {};
        }
        // A repeat is another press, as a held key's own repeats are.
        return {input::Key{.scan = *scan, .down = input.action != kUp}};
    }
    case OSDROP_INPUT_SCROLL: {
        const Wheel wheel =
            wheel_.convert(static_cast<ScrollUnit>(input.scroll_unit), input.dx, input.dy);
        if (wheel.vertical == 0 && wheel.horizontal == 0) {
            return {}; // a fraction, carried to the next scroll
        }
        return {
            move_to(input),
            input::Scroll{
                .vertical = std::clamp(wheel.vertical, -input::kMaxScroll, input::kMaxScroll),
                .horizontal = std::clamp(wheel.horizontal, -input::kMaxScroll, input::kMaxScroll)}};
    }
    case OSDROP_INPUT_CAPS_LOCK:
        if ((input.action == kDown) == caps_lock_on) {
            return {}; // already as the other computer left it
        }
        return {input::Key{.scan = kCapsLock, .down = true},
                input::Key{.scan = kCapsLock, .down = false}};
    default:
        return {};
    }
}

} // namespace osdrop::win
