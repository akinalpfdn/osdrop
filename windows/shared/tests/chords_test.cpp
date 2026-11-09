// Osdrop's key chords on a PC (DEVPLAN Phases 11 and 17): Ctrl+Alt+Windows with Escape or K by
// default, or the user's, matched on the keys as the hooks see them, the completing key kept from
// the apps down to its release.
#include <osdrop/win/chords.hpp>

#include <gtest/gtest.h>

namespace {

namespace input = osdrop::win::input;
using Outcome = input::ChordMatcher::Outcome;
using osdrop::win::ScanCode;

const ScanCode kLeftControl{.code = 0x1D, .extended = false};
const ScanCode kRightControl{.code = 0x1D, .extended = true};
const ScanCode kLeftAlt{.code = 0x38, .extended = false};
const ScanCode kRightAlt{.code = 0x38, .extended = true};
const ScanCode kLeftWindows{.code = 0x5B, .extended = true};
const ScanCode kRightShift{.code = 0x36, .extended = false};
const ScanCode kEscape{.code = 0x01, .extended = false};
const ScanCode kK{.code = 0x25, .extended = false};
const ScanCode kL{.code = 0x26, .extended = false};

void hold(input::ChordMatcher& matcher, std::initializer_list<ScanCode> keys) {
    for (const ScanCode key : keys) {
        EXPECT_EQ(matcher.key(key, true, true), Outcome::Pass) << "modifiers reach the apps";
    }
}

TEST(Chords, ControlAltWindowsEscapeReturnsHome) {
    input::ChordMatcher matcher;
    hold(matcher, {kLeftControl, kLeftAlt, kLeftWindows});
    EXPECT_EQ(matcher.key(kEscape, true, true), Outcome::ReturnHome);
    EXPECT_EQ(matcher.key(kEscape, true, true), Outcome::Swallow) << "a repeat";
    EXPECT_EQ(matcher.key(kEscape, false, true), Outcome::Swallow) << "its release";
    EXPECT_EQ(matcher.key(kEscape, false, true), Outcome::Pass) << "already released";
}

TEST(Chords, ControlAltWindowsKLocksTheInput) {
    input::ChordMatcher matcher;
    hold(matcher, {kRightControl, kRightAlt, kLeftWindows, kRightShift});
    EXPECT_EQ(matcher.modifiers(), input::kControl | input::kAlt | input::kWindows | input::kShift);
    EXPECT_EQ(matcher.key(kK, true, true), Outcome::LockInput) << "more modifiers do not matter";
    EXPECT_EQ(matcher.key(kL, true, true), Outcome::Pass) << "L stays Windows' own";
}

TEST(Chords, AMissingModifierIsNoChord) {
    input::ChordMatcher matcher;
    hold(matcher, {kLeftControl, kLeftAlt});
    EXPECT_EQ(matcher.key(kEscape, true, true), Outcome::Pass);
    EXPECT_EQ(matcher.key(kEscape, false, true), Outcome::Pass);
}

TEST(Chords, BothKeysOfAModifierAreFollowedApart) {
    input::ChordMatcher matcher;
    hold(matcher, {kLeftControl, kRightControl, kLeftAlt, kLeftWindows});
    EXPECT_EQ(matcher.key(kLeftControl, false, true), Outcome::Pass);
    EXPECT_EQ(matcher.key(kEscape, true, true), Outcome::ReturnHome) << "right Control still held";
    EXPECT_EQ(matcher.key(kEscape, false, true), Outcome::Swallow);
    EXPECT_EQ(matcher.key(kRightControl, false, true), Outcome::Pass);
    EXPECT_EQ(matcher.key(kEscape, true, true), Outcome::Pass);
}

TEST(Chords, InactiveChordsPassButModifiersAreStillFollowed) {
    input::ChordMatcher matcher;
    EXPECT_EQ(matcher.key(kLeftControl, true, false), Outcome::Pass);
    EXPECT_EQ(matcher.key(kLeftAlt, true, false), Outcome::Pass);
    EXPECT_EQ(matcher.key(kLeftWindows, true, false), Outcome::Pass);
    EXPECT_EQ(matcher.key(kK, true, false), Outcome::Pass) << "not Osdrop's while inactive";
    EXPECT_EQ(matcher.key(kK, false, false), Outcome::Pass);
    EXPECT_EQ(matcher.key(kK, true, true), Outcome::LockInput) << "active from the next press";
    EXPECT_EQ(matcher.key(kK, false, false), Outcome::Swallow)
        << "a swallowed press is released unseen, even after watching stopped";
}

TEST(Chords, TheUsersChordsReplaceTheDefaults) {
    input::ChordMatcher matcher;
    const input::Chord home{.key = kL, .modifiers = input::kControl | input::kAlt};
    const input::Chord lock{.key = kL,
                            .modifiers = input::kControl | input::kAlt | input::kWindows};
    matcher.set(home, lock);
    hold(matcher, {kLeftControl, kLeftAlt, kLeftWindows});
    EXPECT_EQ(matcher.key(kEscape, true, true), Outcome::Pass) << "the default is gone";
    EXPECT_EQ(matcher.key(kL, true, true), Outcome::LockInput) << "the more specific chord wins";
    EXPECT_EQ(matcher.key(kL, false, true), Outcome::Swallow);
    EXPECT_EQ(matcher.key(kLeftWindows, false, true), Outcome::Pass);
    EXPECT_EQ(matcher.key(kL, true, true), Outcome::ReturnHome);
}

TEST(Chords, ChordsTheHookCannotMatchAreRefused) {
    EXPECT_TRUE(input::valid(input::kReturnHomeChord));
    EXPECT_FALSE(input::valid({.key = kK, .modifiers = input::kShift})) << "Shift alone types";
    EXPECT_FALSE(input::valid({.key = kK, .modifiers = 0}));
    EXPECT_FALSE(input::valid({.key = kLeftAlt, .modifiers = input::kControl})) << "a modifier";
    EXPECT_FALSE(input::valid(
        {.key = ScanCode{.code = 0x7F, .extended = true}, .modifiers = input::kControl}))
        << "no key has it";
    EXPECT_FALSE(input::valid({.key = kK, .modifiers = 0x10}));
    EXPECT_FALSE(input::valid_pair(input::kLockInputChord, input::kLockInputChord));

    input::ChordMatcher matcher;
    matcher.set({.key = kK, .modifiers = input::kShift}, input::kLockInputChord);
    EXPECT_EQ(matcher.return_home(), input::kReturnHomeChord) << "an invalid pair is ignored";
}

} // namespace
