#pragma once

// How OsdropInput logs the clients it turns away (DEVPLAN Phase 19). A client that keeps trying,
// such as another copy of Osdrop, would otherwise write a line per attempt and roll the log over
// within hours. The first refusal of each kind is logged; later ones are counted and logged as one
// line at most every kSummaryInterval. Accepting a client logs what is still counted and starts
// over.

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace osdrop::win::input {

class RefusalTally {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::minutes kSummaryInterval{10};

    /// A line to log about `refusal`.
    struct Line {
        std::string refusal;
        /// 0: the first refusal of its kind; otherwise refused this many more times since the
        /// last line about it.
        std::uint64_t more = 0;
        bool operator==(const Line&) const = default;
    };

    /// A client was refused; `refusal` names why and who ("a client run from C:\..."). The line
    /// to log now, or nullopt while it is only counted.
    [[nodiscard]] std::optional<Line> refused(const std::string& refusal, Clock::time_point now);
    /// A client was accepted: the refusals counted and not logged yet, after which the tally
    /// starts over.
    [[nodiscard]] std::vector<Line> accepted();

private:
    struct Entry {
        Clock::time_point logged;
        std::uint64_t unlogged = 0;
    };
    std::map<std::string, Entry> entries_;
};

} // namespace osdrop::win::input
