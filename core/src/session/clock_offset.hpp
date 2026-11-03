#pragma once

// Peer clock offset from Ping/Pong exchanges. A sample is only as accurate as its round trip is
// symmetric, and a delayed reply (Wi-Fi retries, a busy peer) can be off by half its round trip,
// i.e. seconds. Like NTP, the estimate is the offset of the fastest exchange among the recent
// ones, so one slow exchange never moves it and a real clock change shows up within the window.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace osdrop::session {

class ClockOffsetEstimator {
public:
    /// Exchanges remembered; at one ping per second the estimate follows the fastest of 8 s.
    static constexpr std::size_t kWindow = 8;

    void add(std::int64_t round_trip_us, std::int64_t offset_us) {
        samples_.at(next_) = Sample{.round_trip_us = round_trip_us, .offset_us = offset_us};
        next_ = (next_ + 1) % kWindow;
        count_ = count_ < kWindow ? count_ + 1 : kWindow;
    }

    /// Peer clock minus local clock; nullopt before the first exchange.
    [[nodiscard]] std::optional<std::int64_t> offset_us() const {
        if (count_ == 0) {
            return std::nullopt;
        }
        const Sample* best = &samples_.front();
        for (std::size_t i = 1; i < count_; ++i) {
            if (samples_.at(i).round_trip_us < best->round_trip_us) {
                best = &samples_.at(i);
            }
        }
        return best->offset_us;
    }

private:
    struct Sample {
        std::int64_t round_trip_us = 0;
        std::int64_t offset_us = 0;
    };
    std::array<Sample, kWindow> samples_{};
    std::size_t next_ = 0;
    std::size_t count_ = 0;
};

} // namespace osdrop::session
