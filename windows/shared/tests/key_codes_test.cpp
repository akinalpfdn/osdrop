// Keys between HID usages and Windows scan codes: every entry round-trips, scan codes are unique
// apart from the documented aliases, and the keys Phase 12 names are there.
#include <osdrop/win/key_codes.hpp>

#include <gtest/gtest.h>

#include <initializer_list>
#include <map>
#include <set>

namespace {

using osdrop::win::HidUsage;
using osdrop::win::key_codes;
using osdrop::win::KeyCode;
using osdrop::win::scan_code_of;
using osdrop::win::ScanCode;
using osdrop::win::usage_of;

constexpr std::uint16_t kKeyboard = 0x07;
constexpr std::uint16_t kConsumer = 0x0C;

HidUsage keyboard(std::uint16_t usage) {
    return {.page = kKeyboard, .usage = usage};
}

/// Usages that come back as another one, and which.
const std::map<HidUsage, HidUsage>& aliases() {
    static const std::map<HidUsage, HidUsage> map{
        {keyboard(0x32), keyboard(0x31)},
        {keyboard(0x7F), {.page = kConsumer, .usage = 0xE2}},
        {keyboard(0x80), {.page = kConsumer, .usage = 0xE9}},
        {keyboard(0x81), {.page = kConsumer, .usage = 0xEA}},
    };
    return map;
}

TEST(KeyCodes, EveryEntryRoundTrips) {
    for (const KeyCode& entry : key_codes()) {
        EXPECT_EQ(scan_code_of(entry.usage), entry.scan) << std::hex << entry.usage.usage;
        const auto alias = aliases().find(entry.usage);
        EXPECT_EQ(usage_of(entry.scan), alias == aliases().end() ? entry.usage : alias->second)
            << std::hex << entry.usage.usage;
    }
}

TEST(KeyCodes, UsagesAndScanCodesAreUnique) {
    std::set<HidUsage> usages;
    std::set<ScanCode> scans;
    for (const KeyCode& entry : key_codes()) {
        EXPECT_TRUE(usages.insert(entry.usage).second) << std::hex << entry.usage.usage;
        if (!aliases().contains(entry.usage)) {
            EXPECT_TRUE(scans.insert(entry.scan).second) << std::hex << entry.scan.code;
        }
    }
}

TEST(KeyCodes, CoversTheKeysOfEveryLayout) {
    // ISO and JIS extra keys, the keypad, F13-F24, modifiers and media keys.
    for (const std::uint16_t usage : std::initializer_list<std::uint16_t>{
             0x31, 0x32, 0x64, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x90, 0x91, 0x53, 0x54, 0x58, 0x62,
             0x63, 0x67, 0x85, 0x68, 0x73, 0xE0, 0xE3, 0xE4, 0xE7, 0x46, 0x47, 0x48, 0x65}) {
        EXPECT_TRUE(scan_code_of(keyboard(usage)).has_value()) << std::hex << usage;
    }
    for (const std::uint16_t usage :
         std::initializer_list<std::uint16_t>{0xB5, 0xB6, 0xB7, 0xCD, 0xE2, 0xE9, 0xEA}) {
        EXPECT_TRUE(scan_code_of({.page = kConsumer, .usage = usage}).has_value())
            << std::hex << usage;
    }
}

TEST(KeyCodes, ExtendedKeysAreToldApartFromTheirTwins) {
    EXPECT_EQ(usage_of({.code = 0x1D, .extended = false}), keyboard(0xE0)) << "left Control";
    EXPECT_EQ(usage_of({.code = 0x1D, .extended = true}), keyboard(0xE4)) << "right Control";
    EXPECT_EQ(usage_of({.code = 0x1C, .extended = true}), keyboard(0x58)) << "keypad Enter";
    EXPECT_EQ(usage_of({.code = 0x48, .extended = true}), keyboard(0x52)) << "up arrow";
    EXPECT_EQ(usage_of({.code = 0x48, .extended = false}), keyboard(0x60)) << "keypad 8";
    EXPECT_EQ(usage_of({.code = 0x45, .extended = true}), keyboard(0x53)) << "Num Lock";
    EXPECT_EQ(usage_of({.code = 0x45, .extended = false}), keyboard(0x48)) << "Pause";
}

TEST(KeyCodes, UnknownKeysHaveNoCode) {
    EXPECT_EQ(scan_code_of(keyboard(0x00)), std::nullopt);
    EXPECT_EQ(scan_code_of({.page = 0x09, .usage = 0x01}), std::nullopt) << "button page";
    EXPECT_EQ(usage_of({.code = 0x7F, .extended = true}), std::nullopt);
}

} // namespace
