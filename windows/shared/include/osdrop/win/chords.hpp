#pragma once

// Osdrop's key chords on a PC (DEVPLAN Phase 11): Ctrl, Alt and Windows with Escape bring the
// cursor home; with K they lock it to the computer it is on, or let it cross again. They are the
// Mac's ⌃⌥⌘ chords on the same physical keys, with K for lock because Windows keeps Win+L for
// locking the PC and no hook can take it. The hook thread feeds every physical key here, in
// order; the key that completes a chord, its repeats and its release never reach an app or another
// computer.

#include <osdrop/win/key_codes.hpp>

#include <cstdint>
#include <optional>

namespace osdrop::win::input {

/// Modifier bits of a chord.
inline constexpr std::uint8_t kControl = 1U << 0U;
inline constexpr std::uint8_t kAlt = 1U << 1U;
inline constexpr std::uint8_t kShift = 1U << 2U;
inline constexpr std::uint8_t kWindows = 1U << 3U;

struct Chord {
    ScanCode key;
    std::uint8_t modifiers = 0; ///< these must be held; more do not matter, so a chord always works
};

inline constexpr Chord kReturnHomeChord{.key = {.code = 0x01, .extended = false},
                                        .modifiers = kControl | kAlt | kWindows};
inline constexpr Chord kLockInputChord{.key = {.code = 0x25, .extended = false},
                                       .modifiers = kControl | kAlt | kWindows};

class ChordMatcher {
public:
    enum class Outcome : std::uint8_t {
        Pass,       ///< not Osdrop's
        Swallow,    ///< a repeat or the release of a chord's key
        ReturnHome, ///< the return chord, just completed
        LockInput,  ///< the lock chord, just completed
    };

    /// One physical key event. `active`: the chords are Osdrop's now (input watched or captured);
    /// modifiers are followed either way, so a chord works the moment it becomes active.
    [[nodiscard]] Outcome key(ScanCode scan, bool down, bool active);

    /// The modifier bits held now.
    [[nodiscard]] std::uint8_t modifiers() const;

private:
    std::uint8_t held_ = 0;             ///< one bit per modifier key, left and right apart
    std::optional<ScanCode> chord_key_; ///< down and swallowed: its repeats and release are too
};

} // namespace osdrop::win::input
