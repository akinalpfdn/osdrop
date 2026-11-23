#include <osdrop/win/refusal_tally.hpp>

#include <gtest/gtest.h>

#include <chrono>

namespace {

using osdrop::win::input::RefusalTally;
using Line = RefusalTally::Line;
using std::chrono::minutes;
using std::chrono::seconds;

const std::string kCopy = "a client run from C:\\Copy\\Osdrop.exe";
const std::string kOther = "a client from another session";

TEST(RefusalTally, TheFirstRefusalOfEachKindIsLoggedAtOnce) {
    RefusalTally tally;
    const auto start = RefusalTally::Clock::time_point{};
    EXPECT_EQ(tally.refused(kCopy, start), (Line{.refusal = kCopy, .more = 0}));
    EXPECT_EQ(tally.refused(kOther, start + seconds(1)), (Line{.refusal = kOther, .more = 0}));
}

TEST(RefusalTally, RepeatsAreCountedAndSummedUpOncePerInterval) {
    RefusalTally tally;
    const auto start = RefusalTally::Clock::time_point{};
    static_cast<void>(tally.refused(kCopy, start));
    for (int i = 1; i <= 50; ++i) { // every 10 s, all within the interval
        EXPECT_FALSE(tally.refused(kCopy, start + seconds(i * 10)).has_value()) << i;
    }
    EXPECT_EQ(tally.refused(kCopy, start + RefusalTally::kSummaryInterval),
              (Line{.refusal = kCopy, .more = 51}))
        << "the 50 counted and this one";
    EXPECT_FALSE(tally.refused(kCopy, start + minutes(11)).has_value())
        << "the next line waits a whole interval again";
    EXPECT_EQ(tally.refused(kCopy, start + minutes(20)), (Line{.refusal = kCopy, .more = 2}));
}

TEST(RefusalTally, AnAcceptedClientHandsOverTheCountAndStartsOver) {
    RefusalTally tally;
    const auto start = RefusalTally::Clock::time_point{};
    static_cast<void>(tally.refused(kCopy, start));
    static_cast<void>(tally.refused(kCopy, start + seconds(2)));
    static_cast<void>(tally.refused(kCopy, start + seconds(4)));
    static_cast<void>(tally.refused(kOther, start + seconds(5)));

    EXPECT_EQ(tally.accepted(), (std::vector<Line>{{.refusal = kCopy, .more = 2}}));
    EXPECT_EQ(tally.refused(kCopy, start + seconds(6)), (Line{.refusal = kCopy, .more = 0}))
        << "a refusal after an accepted client is the first again";
    EXPECT_TRUE(tally.accepted().empty());
}

} // namespace
