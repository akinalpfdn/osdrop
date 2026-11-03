#include <osdrop/win/txt_mapping.hpp>

#include <gtest/gtest.h>

#include <string>

namespace {

using osdrop::win::label_of;
using osdrop::win::txt_pairs;
using osdrop::win::txt_record;

TEST(TxtMapping, PairsRoundTripThroughTheRecord) {
    const osdrop::win::TxtPairs pairs{{"pv", "1"},
                                      {"pm", "0"},
                                      {"id", "7a91869e4c114964206972403b0a3d81"},
                                      {"name", "Çalışma PC"},
                                      {"os", "windows"},
                                      {"grp", ""},
                                      {"unknown", "ignored"}};
    const osdrop_txt_record record = txt_record(pairs);
    osdrop_advertisement advertisement{};
    ASSERT_EQ(osdrop_advertisement_from_txt(&record, &advertisement), OSDROP_OK);
    EXPECT_EQ(advertisement.platform, OSDROP_PLATFORM_WINDOWS);
    EXPECT_EQ(advertisement.has_group, 0);
    EXPECT_EQ(std::string(advertisement.name), "Çalışma PC");
    const auto written = txt_pairs(record);
    EXPECT_EQ(written.size(), 6U);
    EXPECT_EQ(written.at(2), (std::pair<std::string, std::string>{"id", pairs.at(2).second}));
}

TEST(TxtMapping, LongValuesAreCutAndMissingKeysLeftEmpty) {
    const osdrop_txt_record record = txt_record({{"name", std::string(200, 'x')}});
    EXPECT_EQ(std::string(record.name).size(), OSDROP_TXT_VALUE_SIZE - 1);
    EXPECT_EQ(std::string(record.device_id), "");
    osdrop_advertisement advertisement{};
    EXPECT_NE(osdrop_advertisement_from_txt(&record, &advertisement), OSDROP_OK);
}

TEST(TxtMapping, LabelsComeFromOsdropInstanceNames) {
    EXPECT_EQ(label_of("abc._osdrop._tcp.local"), "abc");
    EXPECT_EQ(label_of("abc._osdrop._tcp.local."), "abc");
    EXPECT_FALSE(label_of("abc._osplay._tcp.local").has_value());
    EXPECT_FALSE(label_of("._osdrop._tcp.local").has_value());
}

} // namespace
