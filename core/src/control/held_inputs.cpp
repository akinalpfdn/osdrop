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
