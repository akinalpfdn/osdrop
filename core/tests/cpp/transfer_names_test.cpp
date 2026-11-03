// Received names on each file system: a corpus of Unicode, emoji, characters Windows refuses,
// reserved device names, trailing dots and spaces, long names and case collisions maps losslessly
// where the target allows and predictably where it does not.
#include "transfer/names.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using osdrop::transfer::map_name;
using osdrop::transfer::TargetPlatform;
using osdrop::transfer::TreeNames;

constexpr auto kMac = TargetPlatform::MacOS;
constexpr auto kWindows = TargetPlatform::Windows;

TEST(TransferNames, NamesBothSystemsHoldMapToThemselves) {
    const std::vector<std::string> corpus{"report.pdf",
                                          "Çalışma Ağacı.docx",
                                          "日本語のファイル.txt",
                                          "😀 party 🎉.png",
                                          "naïve café.md",
                                          "Ελληνικά",
                                          "a b  c",
                                          ".hidden",
                                          "archive.tar.gz",
                                          "COM10.txt",
                                          "CONSOLE",
                                          "русский текст"};
    for (const std::string& name : corpus) {
        EXPECT_EQ(map_name(name, kMac), name) << name;
        EXPECT_EQ(map_name(name, kWindows), name) << name;
    }
}

TEST(TransferNames, DecomposedNamesArriveComposed) {
    EXPECT_EQ(map_name("Café.txt", kWindows), "Café.txt");
    EXPECT_EQ(map_name("Café.txt", kMac), "Café.txt");
}

TEST(TransferNames, CharactersWindowsRefusesBecomeLookAlikes) {
    EXPECT_EQ(map_name("Q&A: what? *yes*.txt", kWindows), "Q&A： what？ ＊yes＊.txt");
    EXPECT_EQ(map_name("a<b>c|d\"e\\f", kWindows), "a＜b＞c｜d＂e＼f");
    EXPECT_EQ(map_name("Q&A: what?.txt", kMac), "Q&A: what?.txt") << "valid on a Mac";
}

TEST(TransferNames, TrailingDotsAndSpacesSurviveOnWindows) {
    EXPECT_EQ(map_name("ends with dot.", kWindows), "ends with dot．");
    EXPECT_EQ(map_name("spaces  ", kWindows), "spaces  ");
    EXPECT_EQ(map_name("dots..", kWindows), "dots．．");
    EXPECT_EQ(map_name("ends with dot.", kMac), "ends with dot.");
}

TEST(TransferNames, ReservedDeviceNamesGetAnUnderscore) {
    EXPECT_EQ(map_name("CON", kWindows), "CON_");
    EXPECT_EQ(map_name("nul.txt", kWindows), "nul_.txt");
    EXPECT_EQ(map_name("Com1.log", kWindows), "Com1_.log");
    EXPECT_EQ(map_name("LPT¹", kWindows), "LPT¹_");
    EXPECT_EQ(map_name("aux.tar.gz", kWindows), "aux_.tar.gz");
    EXPECT_EQ(map_name("CON", kMac), "CON");
}

TEST(TransferNames, LongNamesAreCutKeepingTheExtension) {
    // 100 three-byte characters: 300 bytes of UTF-8 (too long for APFS), 100 UTF-16 units.
    std::string long_name;
    for (int i = 0; i < 100; ++i) {
        long_name += "ğ";
    }
    long_name += ".txt";
    const std::string on_mac = map_name(long_name, kMac);
    EXPECT_LE(on_mac.size(), 255U);
    EXPECT_TRUE(on_mac.ends_with(".txt"));
    EXPECT_EQ(map_name(long_name, kWindows), long_name) << "fits NTFS's 255 UTF-16 units";
}

TEST(TransferNames, OneFolderNeverHoldsTwoNamesThatDifferOnlyInCase) {
    TreeNames tree(kWindows);
    EXPECT_EQ(tree.map("Docs", true), "Docs");
    EXPECT_EQ(tree.map("docs", true), "docs (2)");
    EXPECT_EQ(tree.map("Docs/a.txt", false), "Docs/a.txt");
    EXPECT_EQ(tree.map("Docs/A.TXT", false), "Docs/A (2).TXT");
    EXPECT_EQ(tree.map("Docs/a.txt", false), "Docs/a (3).txt");
    EXPECT_EQ(tree.map("docs/a.txt", false), "docs (2)/a.txt") << "another folder";
    EXPECT_EQ(tree.map("Café", false), "Café");
    EXPECT_EQ(tree.map("Café", false), "Café (2)") << "the same name once composed";
    EXPECT_EQ(tree.map("missing/x", false), std::nullopt) << "parent not mapped before";
    EXPECT_EQ(tree.map("../escape", false), std::nullopt);
}

} // namespace
