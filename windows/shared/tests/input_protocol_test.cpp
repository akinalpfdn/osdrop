// The input pipe's messages (DEVPLAN Phase 3): every request and report survives a round trip,
// and malformed or out-of-range messages are refused with the right error.
#include <osdrop/win/input_protocol.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

namespace input = osdrop::win::input;
using input::DecodeError;
using osdrop::win::ScanCode;

const ScanCode kA{.code = 0x1E, .extended = false};
const ScanCode kRightArrow{.code = 0x4D, .extended = true};

TEST(InputProtocol, EveryRequestRoundTrips) {
    const std::vector<input::Request> requests{
        input::BeginControl{},
        input::EndControl{},
        input::Move{.x = -input::kMaxCoordinate, .y = input::kMaxCoordinate},
        input::Press{.button = input::Button::X2, .down = true},
        input::Scroll{.vertical = -input::kMaxScroll, .horizontal = 30},
        input::Key{.scan = kRightArrow, .down = true},
        input::StartCapture{.park_x = 1280, .park_y = 720, .home_x = 2559, .home_y = -40},
        input::StopCapture{.x = -1920, .y = 0},
        input::QueryStats{},
        input::Watch{.on = true},
        input::Watch{.on = false},
        input::Heartbeat{},
        input::HoldDrag{},
        input::ReleaseDrag{},
        input::PressForDrag{},
        input::SetChords{},
        input::SetChords{.return_home = {.key = kRightArrow, .modifiers = input::kControl},
                         .lock_input = {.key = kA, .modifiers = input::kAlt | input::kShift}},
    };
    for (const input::Request& request : requests) {
        const auto bytes = input::encode(request);
        ASSERT_LE(bytes.size(), input::kMaxMessageSize);
        const auto decoded = input::decode_request(bytes);
        ASSERT_TRUE(decoded.has_value()) << input::type_name(input::type_of(bytes));
        EXPECT_EQ(decoded.value(), request);
        EXPECT_EQ(input::type_of(request), input::type_of(bytes));
    }
}

TEST(InputProtocol, EveryReportRoundTrips) {
    const std::vector<input::Report> reports{
        input::CapturedMotion{.dx = -3, .dy = 12, .ticks = 1ULL << 50U},
        input::WatchedMotion{.x = 2565, .y = -1, .dx = 7, .dy = -2, .ticks = 123'456'789},
        input::PhysicalPress{.button = input::Button::Middle, .down = false},
        input::PhysicalScroll{.vertical = 120, .horizontal = -40},
        input::PhysicalKey{.scan = kA, .down = true},
        input::LocalInput{},
        input::Stats{.events = 1ULL << 40U, .p50_us = 4, .p99_us = 90, .max_us = 2'000},
        input::Rejected{.type = 3, .reason = input::Refusal::NotControlling},
        input::Rejected{.type = 15, .reason = input::Refusal::NotHeld},
        input::HotkeyPressed{.hotkey = input::Hotkey::LockInput},
        input::Released{.reason = input::ReleaseReason::Watchdog},
    };
    for (const input::Report& report : reports) {
        const auto bytes = input::encode(report);
        ASSERT_LE(bytes.size(), input::kMaxMessageSize);
        const auto decoded = input::decode_report(bytes);
        ASSERT_TRUE(decoded.has_value()) << input::type_name(input::type_of(bytes));
        EXPECT_EQ(decoded.value(), report);
    }
}

DecodeError request_error(const std::vector<std::uint8_t>& bytes) {
    const auto decoded = input::decode_request(bytes);
    EXPECT_FALSE(decoded.has_value());
    return decoded ? DecodeError::Truncated : decoded.error();
}

TEST(InputProtocol, BrokenMessagesAreRefused) {
    const auto good = input::encode(input::Request(input::Move{.x = 10, .y = 20}));
    EXPECT_EQ(request_error({good.begin(), good.begin() + 5}), DecodeError::Truncated);
    auto foreign = good;
    foreign[0] = 'X';
    EXPECT_EQ(request_error(foreign), DecodeError::BadMagic);
    auto newer = good;
    newer[4] = input::kVersion + 1;
    EXPECT_EQ(request_error(newer), DecodeError::UnsupportedVersion);
    auto unknown = good;
    unknown[6] = 77;
    EXPECT_EQ(request_error(unknown), DecodeError::UnknownType);
    auto wide = good;
    wide[7] = 1; // 256 + Move's type: not Move
    EXPECT_EQ(request_error(wide), DecodeError::UnknownType);
    auto longer = good;
    longer.push_back(0);
    EXPECT_EQ(request_error(longer), DecodeError::LengthMismatch);
    auto stretched = longer;
    stretched[8] = 9; // the length field agrees, the body does not
    EXPECT_EQ(request_error(stretched), DecodeError::LengthMismatch);
    const auto report = input::encode(input::Report(input::LocalInput{}));
    EXPECT_EQ(request_error(report), DecodeError::UnknownType) << "a report is no request";
}

TEST(InputProtocol, ValuesOutOfRangeAreRefused) {
    EXPECT_EQ(request_error(input::encode(
                  input::Request(input::Move{.x = input::kMaxCoordinate + 1, .y = 0}))),
              DecodeError::ValueOutOfRange);
    EXPECT_EQ(request_error(input::encode(input::Request(
                  input::Scroll{.vertical = 0, .horizontal = -input::kMaxScroll - 1}))),
              DecodeError::ValueOutOfRange);
    auto button = input::encode(input::Request(input::Press{.button = input::Button::Left}));
    button[input::kHeaderSize] = 5;
    EXPECT_EQ(request_error(button), DecodeError::ValueOutOfRange);
    auto down = button;
    down[input::kHeaderSize] = 0;
    down[input::kHeaderSize + 1] = 2;
    EXPECT_EQ(request_error(down), DecodeError::ValueOutOfRange);
    EXPECT_EQ(request_error(input::encode(input::Request(
                  input::Key{.scan = ScanCode{.code = 0x7F, .extended = true}, .down = true}))),
              DecodeError::ValueOutOfRange)
        << "no key has that scan code";
    auto rejected = input::encode(input::Report(input::Rejected{.type = 3}));
    rejected[input::kHeaderSize + 2] = 9;
    EXPECT_FALSE(input::decode_report(rejected).has_value());
    auto watch = input::encode(input::Request(input::Watch{.on = true}));
    watch[input::kHeaderSize] = 2;
    EXPECT_EQ(request_error(watch), DecodeError::ValueOutOfRange);
    for (const std::uint8_t code : {std::uint8_t{0}, std::uint8_t{3}}) {
        auto hotkey = input::encode(input::Report(input::HotkeyPressed{}));
        hotkey[input::kHeaderSize] = code;
        EXPECT_FALSE(input::decode_report(hotkey).has_value()) << "hotkey " << int{code};
        auto released = input::encode(input::Report(input::Released{}));
        released[input::kHeaderSize] = code;
        EXPECT_FALSE(input::decode_report(released).has_value()) << "release reason " << int{code};
    }
    EXPECT_FALSE(input::decode_report(input::encode(input::Report(
                                          input::WatchedMotion{.x = input::kMaxCoordinate + 1})))
                     .has_value());
    const input::Chord shift_only{.key = kA, .modifiers = input::kShift};
    EXPECT_EQ(request_error(input::encode(input::Request(input::SetChords{
                  .return_home = shift_only, .lock_input = input::kLockInputChord}))),
              DecodeError::ValueOutOfRange)
        << "Shift alone types";
    EXPECT_EQ(request_error(input::encode(input::Request(input::SetChords{
                  .return_home = input::kLockInputChord, .lock_input = input::kLockInputChord}))),
              DecodeError::ValueOutOfRange)
        << "the same chord twice";
}

TEST(InputProtocol, TypesHaveNamesForTheLog) {
    EXPECT_EQ(
        input::type_name(input::type_of(input::encode(input::Request(input::Key{.scan = kA})))),
        "Key");
    EXPECT_EQ(input::type_name(999), "unknown");
    EXPECT_EQ(input::type_of(std::vector<std::uint8_t>{1, 2}), 0);
}

} // namespace
