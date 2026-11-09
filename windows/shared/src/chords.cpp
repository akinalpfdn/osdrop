#include <osdrop/win/chords.hpp>

#include <array>

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

} // namespace

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
    if (scan == kReturnHomeChord.key && held(modifiers, kReturnHomeChord)) {
        chord_key_ = scan;
        return Outcome::ReturnHome;
    }
    if (scan == kLockInputChord.key && held(modifiers, kLockInputChord)) {
        chord_key_ = scan;
        return Outcome::LockInput;
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
