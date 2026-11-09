#include <osdrop/win/chords.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <utility>

namespace osdrop::win::input {
namespace {

struct ModifierKey {
    ScanCode scan;
    std::uint8_t modifier = 0;
};

// Left and right keys of each modifier, as low-level hooks report them.
constexpr std::array<ModifierKey, 8> kModifierKeys{{
    {.scan = {.code = 0x1D, .extended = false}, .modifier = kControl},
    {.scan = {.code = 0x1D, .extended = true}, .modifier = kControl},
    {.scan = {.code = 0x38, .extended = false}, .modifier = kAlt},
    {.scan = {.code = 0x38, .extended = true}, .modifier = kAlt},
    {.scan = {.code = 0x2A, .extended = false}, .modifier = kShift},
    {.scan = {.code = 0x36, .extended = false}, .modifier = kShift},
    {.scan = {.code = 0x5B, .extended = true}, .modifier = kWindows},
    {.scan = {.code = 0x5C, .extended = true}, .modifier = kWindows},
}};

bool held(std::uint8_t modifiers, const Chord& chord) {
    return (modifiers & chord.modifiers) == chord.modifiers;
}

bool is_modifier(ScanCode scan) {
    return std::ranges::any_of(kModifierKeys,
                               [&](const ModifierKey& modifier) { return modifier.scan == scan; });
}

} // namespace

bool valid(const Chord& chord) {
    return (chord.modifiers & ~kChordModifiers) == 0 &&
           (chord.modifiers & (kControl | kAlt | kWindows)) != 0 && usage_of(chord.key) &&
           !is_modifier(chord.key);
}

bool valid_pair(const Chord& return_home, const Chord& lock_input) {
    return valid(return_home) && valid(lock_input) && return_home != lock_input;
}

void ChordMatcher::set(const Chord& return_home, const Chord& lock_input) {
    if (valid_pair(return_home, lock_input)) {
        return_home_ = return_home;
        lock_input_ = lock_input;
    }
}

ChordMatcher::Outcome ChordMatcher::key(ScanCode scan, bool down, bool active) {
    unsigned bit = 1;
    for (const ModifierKey& modifier : kModifierKeys) {
        if (modifier.scan == scan) {
            held_ = static_cast<std::uint8_t>(down ? held_ | bit : held_ & ~bit);
            return Outcome::Pass;
        }
        bit <<= 1U;
    }
    if (chord_key_ && *chord_key_ == scan) {
        if (!down) {
            chord_key_.reset();
        }
        return Outcome::Swallow; // its press was Osdrop's, so are its repeats and release
    }
    if (!down || !active) {
        return Outcome::Pass;
    }
    const std::uint8_t modifiers = this->modifiers();
    // With the same key, the chord with more modifiers is tried first, so both can be reached.
    const bool lock_first =
        std::popcount(lock_input_.modifiers) > std::popcount(return_home_.modifiers);
    const std::array<std::pair<const Chord*, Outcome>, 2> chords{{
        {lock_first ? &lock_input_ : &return_home_,
         lock_first ? Outcome::LockInput : Outcome::ReturnHome},
        {lock_first ? &return_home_ : &lock_input_,
         lock_first ? Outcome::ReturnHome : Outcome::LockInput},
    }};
    for (const auto& [chord, outcome] : chords) {
        if (scan == chord->key && held(modifiers, *chord)) {
            chord_key_ = scan;
            return outcome;
        }
    }
    return Outcome::Pass;
}

std::uint8_t ChordMatcher::modifiers() const {
    std::uint8_t modifiers = 0;
    unsigned bit = 1;
    for (const ModifierKey& modifier : kModifierKeys) {
        if ((held_ & bit) != 0) {
            modifiers = static_cast<std::uint8_t>(modifiers | modifier.modifier);
        }
        bit <<= 1U;
    }
    return modifiers;
}

} // namespace osdrop::win::input
