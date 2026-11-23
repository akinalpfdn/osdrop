#include <osdrop/win/refusal_tally.hpp>

namespace osdrop::win::input {

std::optional<RefusalTally::Line> RefusalTally::refused(const std::string& refusal,
                                                        Clock::time_point now) {
    const auto [it, first] = entries_.try_emplace(refusal, Entry{.logged = now, .unlogged = 0});
    if (first) {
        return Line{.refusal = refusal, .more = 0};
    }
    Entry& entry = it->second;
    ++entry.unlogged;
    if (now - entry.logged < kSummaryInterval) {
        return std::nullopt;
    }
    const Line line{.refusal = refusal, .more = entry.unlogged};
    entry.logged = now;
    entry.unlogged = 0;
    return line;
}

std::vector<RefusalTally::Line> RefusalTally::accepted() {
    std::vector<Line> lines;
    for (const auto& [refusal, entry] : entries_) {
        if (entry.unlogged > 0) {
            lines.push_back(Line{.refusal = refusal, .more = entry.unlogged});
        }
    }
    entries_.clear();
    return lines;
}

} // namespace osdrop::win::input
