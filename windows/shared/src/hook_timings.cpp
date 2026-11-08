#include <osdrop/win/hook_timings.hpp>

#include <algorithm>

namespace osdrop::win::input {

void HookTimings::record(std::uint32_t microseconds) {
    const std::size_t bucket = std::min<std::size_t>(microseconds, kBuckets - 1);
    counts_.at(bucket).fetch_add(1, std::memory_order_relaxed);
    std::uint32_t seen = max_.load(std::memory_order_relaxed);
    while (microseconds > seen &&
           !max_.compare_exchange_weak(seen, microseconds, std::memory_order_relaxed)) {
    }
}

Stats HookTimings::stats() const {
    std::array<std::uint64_t, kBuckets> counts{};
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < kBuckets; ++i) {
        counts.at(i) = counts_.at(i).load(std::memory_order_relaxed);
        total += counts.at(i);
    }
    const auto percentile = [&](std::uint64_t per_mille) -> std::uint32_t {
        if (total == 0) {
            return 0;
        }
        // The smallest duration that at least this share of the callbacks did not exceed.
        const std::uint64_t rank = ((total * per_mille) + 999) / 1000;
        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < kBuckets; ++i) {
            seen += counts.at(i);
            if (seen >= rank) {
                return static_cast<std::uint32_t>(i);
            }
        }
        return static_cast<std::uint32_t>(kBuckets - 1);
    };
    return {.events = total,
            .p50_us = percentile(500),
            .p99_us = percentile(990),
            .max_us = max_.load(std::memory_order_relaxed)};
}

} // namespace osdrop::win::input
