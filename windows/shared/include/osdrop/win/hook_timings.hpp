#pragma once

// How long OsdropInput's low-level hook callbacks take (DEVPLAN Phase 3: under 1 ms at p99, or
// Windows removes the hook). Recorded from the hook thread without locks, read from any thread.

#include <osdrop/win/input_protocol.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace osdrop::win::input {

class HookTimings {
public:
    /// One callback's duration; durations past the last bucket count in it, and in the maximum.
    void record(std::uint32_t microseconds);
    /// Percentiles to the microsecond below kBuckets, the last bucket's value above.
    [[nodiscard]] Stats stats() const;

    static constexpr std::size_t kBuckets = 4096;

private:
    std::array<std::atomic<std::uint64_t>, kBuckets> counts_{};
    std::atomic<std::uint32_t> max_{0};
};

} // namespace osdrop::win::input
