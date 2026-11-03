// NFC normalization against vectors Python's unicodedata computed (core/tools), UTF-8 checks, and
// the case folding used to find names a case-insensitive file system treats as one.
#include "transfer/unicode.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <string>

namespace {

using osdrop::transfer::fold_case;
using osdrop::transfer::to_nfc;

std::string from_hex(const std::string& hex) {
    std::string bytes;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

TEST(TransferUnicode, NormalizesToFormCLikeTheUnicodeDatabase) {
    std::ifstream vectors(std::string(OSDROP_TEST_DATA_DIR) + "/nfc_vectors.txt");
    ASSERT_TRUE(vectors.is_open());
    std::string line;
    std::size_t checked = 0;
    while (std::getline(vectors, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto tab = line.find('\t');
        ASSERT_NE(tab, std::string::npos);
        const std::string input = from_hex(line.substr(0, tab));
        const std::string expected = from_hex(line.substr(tab + 1));
        const auto normalized = to_nfc(input);
        ASSERT_TRUE(normalized.has_value()) << line;
        EXPECT_EQ(*normalized, expected) << line;
        EXPECT_EQ(to_nfc(*normalized), normalized) << "NFC is idempotent: " << line;
        ++checked;
    }
    EXPECT_EQ(checked, 3000U);
}

TEST(TransferUnicode, CommonCases) {
    EXPECT_EQ(to_nfc("plain.txt"), "plain.txt");
    EXPECT_EQ(to_nfc("Café"), "Café") << "decomposed, as macOS often writes names";
    EXPECT_EQ(to_nfc("각"), "각") << "Hangul jamo compose";
    EXPECT_EQ(to_nfc("Å"), "Å") << "Angstrom sign is a singleton";
    EXPECT_EQ(to_nfc("क़"), "क़") << "composition exclusion stays apart";
    EXPECT_EQ(to_nfc("ą́"), "ą́");
    EXPECT_EQ(to_nfc("ş"), "ş");
    EXPECT_FALSE(to_nfc("\xC0\x80").has_value()) << "overlong";
    EXPECT_FALSE(to_nfc("\xED\xA0\x80").has_value()) << "surrogate";
    EXPECT_FALSE(to_nfc("\xF4\x90\x80\x80").has_value()) << "above U+10FFFF";
    EXPECT_EQ(osdrop::transfer::unicode_version(), "15.1.0");
}

TEST(TransferUnicode, FoldsCaseForCollisionChecks) {
    EXPECT_EQ(fold_case("Report.TXT"), fold_case("report.txt"));
    EXPECT_EQ(fold_case("ÇALIŞMA"), fold_case("çalişma"));
    EXPECT_EQ(fold_case("Café"), fold_case("CAFÉ"));
    EXPECT_NE(fold_case("straße"), fold_case("STRASSE")) << "simple folding, as file systems do";
}

} // namespace
