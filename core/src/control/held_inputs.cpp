#include "control/held_inputs.hpp"

#include <algorithm>

namespace osdrop::control {

namespace {

template <typename T>
bool record(std::vector<T>& held, const T& item, bool down) {
    const auto found = std::ranges::find(held, item);
    if (down) {
        if (found == held.end()) {
            held.push_back(item);
        }
        return true;
    }
    if (found == held.end()) {
        return false;
    }
    held.erase(found);
    return true;
}

} // namespace

bool HeldInputs::key(const HeldKey& key, protocol::KeyAction action) {
    return record(keys_, key, action != protocol::KeyAction::Up);
}

bool HeldInputs::button(std::uint8_t button, protocol::ButtonAction action) {
    return record(buttons_, button, action == protocol::ButtonAction::Down);
}

void HeldInputs::clear() {
    keys_.clear();
    buttons_.clear();
}

std::uint8_t HeldInputs::modifiers() const {
    unsigned bits = 0;
    for (const HeldKey& key : keys_) {
        if (key.page == protocol::kKeyboardPage && key.usage >= kFirstModifierUsage &&
            key.usage <= kLastModifierUsage) {
            bits |= 1U << static_cast<unsigned>(key.usage - kFirstModifierUsage);
        }
    }
    return static_cast<std::uint8_t>(bits);
}

namespace {

constexpr std::uint16_t kLeftControl = 0xE0;
constexpr std::uint16_t kLeftCommand = 0xE3;
constexpr std::uint16_t kRightControl = 0xE4;
constexpr std::uint16_t kRightCommand = 0xE7;

unsigned bit_of(std::uint16_t usage) {
    return 1U << static_cast<unsigned>(usage - kFirstModifierUsage);
}

} // namespace

HeldKey swap_control_and_command(const HeldKey& key) {
    if (key.page != protocol::kKeyboardPage) {
        return key;
    }
    switch (key.usage) {
    case kLeftControl:
        return {.page = key.page, .usage = kLeftCommand};
    case kLeftCommand:
        return {.page = key.page, .usage = kLeftControl};
    case kRightControl:
        return {.page = key.page, .usage = kRightCommand};
    case kRightCommand:
        return {.page = key.page, .usage = kRightControl};
    default:
        return key;
    }
}

std::uint8_t swap_control_and_command(std::uint8_t modifiers) {
    const unsigned bits = modifiers;
    const unsigned swapped = (bits & ~(bit_of(kLeftControl) | bit_of(kLeftCommand) |
                                       bit_of(kRightControl) | bit_of(kRightCommand))) |
                             ((bits & bit_of(kLeftControl)) != 0 ? bit_of(kLeftCommand) : 0U) |
                             ((bits & bit_of(kLeftCommand)) != 0 ? bit_of(kLeftControl) : 0U) |
                             ((bits & bit_of(kRightControl)) != 0 ? bit_of(kRightCommand) : 0U) |
                             ((bits & bit_of(kRightCommand)) != 0 ? bit_of(kRightControl) : 0U);
    return static_cast<std::uint8_t>(swapped);
}

std::vector<HeldKey> modifier_keys(std::uint8_t modifiers) {
    std::vector<HeldKey> keys;
    for (std::uint16_t usage = kFirstModifierUsage; usage <= kLastModifierUsage; ++usage) {
        if ((modifiers & (1U << static_cast<unsigned>(usage - kFirstModifierUsage))) != 0) {
            keys.push_back({.page = protocol::kKeyboardPage, .usage = usage});
        }
    }
    return keys;
}

} // namespace osdrop::control
