#pragma once

// Keys and buttons held down (DEVPLAN, "Roles and topology": everything pressed is released). The
// controller keeps one for its physical input, an endpoint one for what it injected, so that every
// way out of control releases exactly what is still down, in reverse order.

#include "protocol/control_messages.hpp"

#include <compare>
#include <cstdint>
#include <vector>

namespace osdrop::control {

struct HeldKey {
    std::uint16_t page = protocol::kKeyboardPage;
    std::uint16_t usage = 0;
    auto operator<=>(const HeldKey&) const = default;
};

/// The modifier keys, keyboard usages 0xE0 (left Control) to 0xE7 (right Command/Windows), are
/// bits 0 to 7 of Enter's `modifiers`.
inline constexpr std::uint16_t kFirstModifierUsage = 0xE0;
inline constexpr std::uint16_t kLastModifierUsage = 0xE7;

class HeldInputs {
public:
    /// Records a key event; false for the release of a key that is not held.
    bool key(const HeldKey& key, protocol::KeyAction action);
    /// Records a button event; false for the release of a button that is not held.
    bool button(std::uint8_t button, protocol::ButtonAction action);
    void clear();

    /// In the order they went down.
    [[nodiscard]] const std::vector<HeldKey>& keys() const { return keys_; }
    [[nodiscard]] const std::vector<std::uint8_t>& buttons() const { return buttons_; }
    /// The held modifier keys, as Enter carries them.
    [[nodiscard]] std::uint8_t modifiers() const;

private:
    std::vector<HeldKey> keys_;
    std::vector<std::uint8_t> buttons_;
};

/// The modifier keys `modifiers` names, left before right.
[[nodiscard]] std::vector<HeldKey> modifier_keys(std::uint8_t modifiers);

inline constexpr std::uint16_t kCapsLockUsage = 0x39;

/// Control and Command (Windows) swapped, on both sides, so shortcuts keep their place between a
/// Mac and a PC: ⌘C on a Mac is Ctrl+C on a PC and the other way (user decision, Phase 12).
[[nodiscard]] HeldKey swap_control_and_command(const HeldKey& key);
[[nodiscard]] std::uint8_t swap_control_and_command(std::uint8_t modifiers);

} // namespace osdrop::control
