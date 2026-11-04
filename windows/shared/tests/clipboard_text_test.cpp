// Clipboard text as Windows holds it: UTF-16 both ways and the CF_HTML header.
#include <osdrop/win/clipboard_text.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using osdrop::win::cf_html_of_html;
using osdrop::win::html_of_cf_html;
using osdrop::win::utf16_of_utf8;
using osdrop::win::utf8_of_utf16;

std::vector<std::uint8_t> bytes(const std::string& text) {
    return {text.begin(), text.end()};
}

TEST(ClipboardText, Utf16RoundTripsEveryPlaneAndStopsAtNul) {
    const std::string text = "Çalışma ağacı · 日本語 · 😀";
    const auto wide = utf16_of_utf8(text);
    ASSERT_TRUE(wide.has_value());
    EXPECT_EQ(utf8_of_utf16(*wide), text);
    EXPECT_EQ(utf8_of_utf16(std::u16string(u"abc\0def", 7)), "abc") << "CF_UNICODETEXT ends at NUL";
    EXPECT_EQ(utf8_of_utf16(std::u16string(1, static_cast<char16_t>(0xD800))), std::nullopt);
    EXPECT_EQ(utf16_of_utf8("\xC0\x80"), std::nullopt) << "overlong";
    EXPECT_EQ(utf16_of_utf8("\xED\xA0\x80"), std::nullopt) << "a surrogate in UTF-8";
}

TEST(ClipboardText, CfHtmlRoundTripsAndItsOffsetsPointAtTheFragment) {
    const std::string html = "<b>bold</b> and <i>ünïcode</i>";
    const auto block = cf_html_of_html(html);
    const std::string text(block.begin(), block.end());
    const auto field = [&](const std::string& key) {
        const auto at = text.find(key + ":");
        return std::stoul(text.substr(at + key.size() + 1, 10));
    };
    EXPECT_EQ(text.substr(field("StartFragment"), field("EndFragment") - field("StartFragment")),
              html);
    EXPECT_EQ(text.substr(field("StartHTML"), 6), "<html>");
    EXPECT_EQ(field("EndHTML"), text.size());

    const auto document = html_of_cf_html(block);
    ASSERT_TRUE(document.has_value());
    EXPECT_NE(document->find(html), std::string::npos);
    EXPECT_EQ(document->substr(0, 6), "<html>");
}

TEST(ClipboardText, CfHtmlFromOtherProducersIsRead) {
    // Only fragment offsets (StartHTML -1), as some producers write, and a trailing NUL.
    const std::string fragment = "<p>hi</p>";
    std::string block = "Version:1.0\r\nStartHTML:-1\r\nEndHTML:-1\r\nStartFragment:0000000089\r\n"
                        "EndFragment:0000000098\r\n";
    block += fragment;
    block.push_back('\0');
    ASSERT_EQ(block.find(fragment), 89U);
    EXPECT_EQ(html_of_cf_html(bytes(block)), fragment);

    EXPECT_EQ(html_of_cf_html(bytes("<p>no header</p>")), std::nullopt);
    EXPECT_EQ(
        html_of_cf_html(bytes("Version:0.9\r\nStartHTML:0000000010\r\nEndHTML:0000099999\r\n")),
        std::nullopt)
        << "offsets past the block";
}

} // namespace
