// The portable parts of OsdropInput's hook path: callback timings and the queue from the hook
// thread to the pipe.
#include <osdrop/win/hook_timings.hpp>
#include <osdrop/win/report_queue.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <variant>
#include <vector>

namespace {

namespace input = osdrop::win::input;
using osdrop::win::ScanCode;

TEST(HookTimings, PercentilesFollowTheRecordedDurations) {
    input::HookTimings timings;
    EXPECT_EQ(timings.stats(), input::Stats{});
    for (std::uint32_t i = 0; i < 980; ++i) {
        timings.record(2);
    }
    for (std::uint32_t i = 0; i < 19; ++i) {
        timings.record(40);
    }
    timings.record(1'000'000); // one callback that took a second
    const input::Stats stats = timings.stats();
    EXPECT_EQ(stats.events, 1000U);
    EXPECT_EQ(stats.p50_us, 2U);
    EXPECT_EQ(stats.p99_us, 40U);
    EXPECT_EQ(stats.max_us, 1'000'000U);
}

TEST(ReportQueue, MotionAddsUpWhileKeysAndButtonsKeepTheirOrder) {
    int wakes = 0;
    input::ReportQueue queue([&] { ++wakes; });
    queue.push(input::CapturedMotion{.dx = 1, .dy = 2, .ticks = 10});
    queue.push(input::CapturedMotion{.dx = 3, .dy = -1, .ticks = 20});
    queue.push(input::PhysicalKey{.scan = ScanCode{.code = 0x1E}, .down = true});
    queue.push(input::CapturedMotion{.dx = 5, .dy = 5, .ticks = 30});
    EXPECT_EQ(wakes, 4);
    const auto taken = queue.take();
    ASSERT_EQ(taken.size(), 3U);
    EXPECT_EQ(std::get<input::CapturedMotion>(taken[0]),
              (input::CapturedMotion{.dx = 4, .dy = 1, .ticks = 10}))
        << "the earliest time stays";
    EXPECT_TRUE(std::holds_alternative<input::PhysicalKey>(taken[1]));
    EXPECT_EQ(std::get<input::CapturedMotion>(taken[2]),
              (input::CapturedMotion{.dx = 5, .dy = 5, .ticks = 30}));
    EXPECT_TRUE(queue.take().empty());
}

TEST(ReportQueue, WatchedMotionKeepsTheNewestPositionAndAddsTheMovement) {
    input::ReportQueue queue({});
    queue.push(input::WatchedMotion{.x = 1919, .y = 500, .dx = 4, .dy = 0, .ticks = 7});
    queue.push(input::WatchedMotion{.x = 1925, .y = 501, .dx = 6, .dy = 1, .ticks = 9});
    queue.push(input::CapturedMotion{.dx = 2, .dy = 2, .ticks = 11});
    const auto taken = queue.take();
    ASSERT_EQ(taken.size(), 2U) << "motion of different kinds is not added together";
    EXPECT_EQ(std::get<input::WatchedMotion>(taken[0]),
              (input::WatchedMotion{.x = 1925, .y = 501, .dx = 10, .dy = 1, .ticks = 7}));
    EXPECT_TRUE(std::holds_alternative<input::CapturedMotion>(taken[1]));
}

TEST(ReportQueue, PastTheLimitOnlyMotionIsDropped) {
    input::ReportQueue queue({}, 3);
    queue.push(input::CapturedMotion{.dx = 1});
    queue.push(input::PhysicalPress{.button = input::Button::Left, .down = true});
    queue.push(input::WatchedMotion{.dx = 2});
    queue.push(input::PhysicalPress{.button = input::Button::Left, .down = false});
    queue.push(input::PhysicalScroll{.vertical = 120});
    const auto taken = queue.take();
    ASSERT_EQ(taken.size(), 3U) << "both motions went, the press and release and the scroll stayed";
    EXPECT_TRUE(std::holds_alternative<input::PhysicalPress>(taken[0]));
    EXPECT_TRUE(std::holds_alternative<input::PhysicalPress>(taken[1]));
    EXPECT_TRUE(std::holds_alternative<input::PhysicalScroll>(taken[2]));
}

TEST(ReportQueue, SummedMotionStaysInRange) {
    input::ReportQueue queue({});
    queue.push(input::CapturedMotion{.dx = input::kMaxCoordinate});
    queue.push(input::CapturedMotion{.dx = input::kMaxCoordinate});
    const auto taken = queue.take();
    ASSERT_EQ(taken.size(), 1U);
    EXPECT_EQ(std::get<input::CapturedMotion>(taken[0]).dx, input::kMaxCoordinate);
}

} // namespace
