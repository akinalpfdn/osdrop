#include <osdrop/osdrop.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

/// Copies a literal and its NUL into a fixed C string field.
template <std::size_t Size>
void set_text(char (&field)[Size], const char* text) { // NOLINT(*-avoid-c-arrays): C API field
    static_cast<void>(std::snprintf(static_cast<char*>(field), Size, "%s", text));
}

osdrop_advertisement grouped_pc() {
    osdrop_advertisement advertisement{};
    advertisement.protocol_version = OSDROP_PROTOCOL_VERSION;
    advertisement.platform = OSDROP_PLATFORM_WINDOWS;
    advertisement.protocol_minor = OSDROP_PROTOCOL_MINOR;
    advertisement.has_group = 1;
    for (std::size_t i = 0; i < OSDROP_DEVICE_ID_SIZE; ++i) {
        advertisement.device_id[i] = static_cast<std::uint8_t>(0xA0 + i);
        advertisement.group_id[i] = static_cast<std::uint8_t>(i);
    }
    set_text(advertisement.name, "Akınalp'ın PC");
    return advertisement;
}

osdrop_txt_record record_of(const osdrop_advertisement& advertisement) {
    osdrop_txt_record record{};
    EXPECT_EQ(osdrop_advertisement_to_txt(&advertisement, &record), OSDROP_OK);
    return record;
}

TEST(Discovery, TheTxtRecordUsesTheDocumentedValues) {
    const osdrop_txt_record record = record_of(grouped_pc());
    EXPECT_STREQ(record.protocol_version, "1");
    EXPECT_STREQ(record.protocol_minor, "0");
    EXPECT_STREQ(record.device_id, "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf");
    EXPECT_STREQ(record.name, "Akınalp'ın PC");
    EXPECT_STREQ(record.os, "windows");
    EXPECT_STREQ(record.group_id, "000102030405060708090a0b0c0d0e0f");
    EXPECT_STREQ(OSDROP_SERVICE_TYPE, "_osdrop._tcp");
}

TEST(Discovery, ARecordReadsBackAsTheSameAdvertisement) {
    osdrop_advertisement mac = grouped_pc();
    mac.platform = OSDROP_PLATFORM_MACOS;
    for (const std::uint8_t has_group : {std::uint8_t{0}, std::uint8_t{1}}) {
        osdrop_advertisement original = has_group == 1 ? grouped_pc() : mac;
        original.has_group = has_group;
        if (has_group == 0) {
            std::memset(original.group_id, 0, sizeof(original.group_id));
        }
        const osdrop_txt_record record = record_of(original);
        EXPECT_EQ(record.group_id[0] == '\0', has_group == 0);
        osdrop_advertisement parsed{};
        ASSERT_EQ(osdrop_advertisement_from_txt(&record, &parsed), OSDROP_OK);
        EXPECT_EQ(std::memcmp(&parsed, &original, sizeof(parsed)), 0);
    }
}

TEST(Discovery, ANewerProtocolVersionIsReportedNotRefused) {
    osdrop_txt_record record = record_of(grouped_pc());
    set_text(record.protocol_version, "2");
    set_text(record.protocol_minor, "255");
    osdrop_advertisement parsed{};
    ASSERT_EQ(osdrop_advertisement_from_txt(&record, &parsed), OSDROP_OK);
    EXPECT_EQ(parsed.protocol_version, 2U);
    EXPECT_EQ(parsed.protocol_minor, 255);
}

TEST(Discovery, MalformedRecordsAreRejected) {
    const osdrop_txt_record good = record_of(grouped_pc());
    osdrop_advertisement parsed{};
    const auto expect = [&](auto change, osdrop_result result, const char* what) {
        osdrop_txt_record record = good;
        change(record);
        EXPECT_EQ(osdrop_advertisement_from_txt(&record, &parsed), result) << what;
    };
    const osdrop_result range = OSDROP_ERROR_VALUE_OUT_OF_RANGE;
    for (const char* version : {"", "0", "01", "+1", "-1", "1x", " 1", "4294967296"}) {
        expect([&](osdrop_txt_record& r) { set_text(r.protocol_version, version); }, range,
               version);
    }
    for (const char* minor : {"", "00", "256", "-0", "0x"}) {
        expect([&](osdrop_txt_record& r) { set_text(r.protocol_minor, minor); }, range, minor);
    }
    expect([](osdrop_txt_record& r) { set_text(r.device_id, "A0A1A2A3A4A5A6A7A8A9AAABACADAEAF"); },
           range, "uppercase device ID");
    expect([](osdrop_txt_record& r) { set_text(r.device_id, "a0a1"); }, range, "short device ID");
    expect([](osdrop_txt_record& r) { set_text(r.group_id, "00000000000000000000000000000000"); },
           range, "zero group ID");
    expect([](osdrop_txt_record& r) { set_text(r.group_id, "0001020304050607"); }, range,
           "short group ID");
    expect([](osdrop_txt_record& r) { set_text(r.os, "linux"); }, range, "unknown os");
    expect([](osdrop_txt_record& r) { set_text(r.os, "macOS"); }, range, "os is case sensitive");
    expect([](osdrop_txt_record& r) { r.name[0] = '\0'; }, OSDROP_ERROR_INVALID_STRING,
           "empty name");
    expect([](osdrop_txt_record& r) { set_text(r.name, "tab\there"); }, OSDROP_ERROR_INVALID_STRING,
           "control character");
    expect([](osdrop_txt_record& r) { std::memset(r.name, 'x', sizeof(r.name)); },
           OSDROP_ERROR_INVALID_STRING, "65 bytes without NUL");
}

TEST(Discovery, InvalidAdvertisementsAreNotPublished) {
    osdrop_txt_record record{};
    const auto expect = [&](auto change, osdrop_result result, const char* what) {
        osdrop_advertisement advertisement = grouped_pc();
        change(advertisement);
        EXPECT_EQ(osdrop_advertisement_to_txt(&advertisement, &record), result) << what;
    };
    const osdrop_result range = OSDROP_ERROR_VALUE_OUT_OF_RANGE;
    expect([](osdrop_advertisement& a) { a.protocol_version = 0; }, range, "version 0");
    expect([](osdrop_advertisement& a) { a.platform = static_cast<osdrop_platform>(3); }, range,
           "platform 3");
    expect([](osdrop_advertisement& a) { a.has_group = 2; }, range, "has_group 2");
    expect([](osdrop_advertisement& a) { std::memset(a.group_id, 0, sizeof(a.group_id)); }, range,
           "zero group ID");
    expect([](osdrop_advertisement& a) { std::memset(a.name, 'x', sizeof(a.name)); },
           OSDROP_ERROR_INVALID_STRING, "65 bytes without NUL");
    expect([](osdrop_advertisement& a) { a.name[0] = '\0'; }, OSDROP_ERROR_INVALID_STRING,
           "empty name");
    EXPECT_EQ(osdrop_advertisement_to_txt(nullptr, &record), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_advertisement_from_txt(&record, nullptr), OSDROP_ERROR_INVALID_ARGUMENT);
}

} // namespace
