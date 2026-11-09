#pragma once

// Reports from OsdropInput's hook thread to its pipe thread (DEVPLAN Phase 3: hook callbacks only
// enqueue). Motion that piles up while the pipe is busy is added into one report; keys, buttons,
// scrolling and the rest are never dropped, so a key never stays down on the other computer. Past
// the limit the oldest motion goes first.

#include <osdrop/win/input_protocol.hpp>

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace osdrop::win::input {

class ReportQueue {
public:
    /// `wake` runs after every push, on the pushing thread; it must be quick (SetEvent).
    explicit ReportQueue(std::function<void()> wake, std::size_t limit = kDefaultLimit);

    void push(const Report& report);
    /// Everything queued, oldest first.
    [[nodiscard]] std::vector<Report> take();

    static constexpr std::size_t kDefaultLimit = 1024;

private:
    std::function<void()> wake_;
    std::size_t limit_;
    std::mutex mutex_;
    std::deque<Report> reports_;
};

} // namespace osdrop::win::input
