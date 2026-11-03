#include <osdrop/win/screens.hpp>

#include <algorithm>
#include <iterator>
#include <string>

namespace osdrop::win {
namespace {

constexpr std::int32_t kUnit = 16;     // coords are 1/16 of a unit
constexpr std::uint32_t kBaseDpi = 96; // 100%
constexpr std::size_t kMaxNameBytes = 64;

std::uint64_t fnv1a(std::string_view text, std::uint64_t seed) {
    std::uint64_t hash = seed;
    for (const char c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

std::int32_t logical(std::int32_t physical, std::uint32_t dpi) {
    const auto scaled = (static_cast<std::int64_t>(physical) * kUnit * kBaseDpi) / dpi;
    return static_cast<std::int32_t>(scaled);
}

/// The length of the UTF-8 sequence that starts with `lead`, 1 for anything unexpected.
std::size_t sequence_length(std::uint8_t lead) {
    if (lead >= 0xF0U) {
        return 4;
    }
    if (lead >= 0xE0U) {
        return 3;
    }
    if (lead >= 0xC0U) {
        return 2;
    }
    return 1;
}

} // namespace

std::array<std::uint8_t, OSDROP_MONITOR_KEY_SIZE> monitor_key(std::string_view path) {
    std::array<std::uint8_t, OSDROP_MONITOR_KEY_SIZE> key{};
    const std::uint64_t first = fnv1a(path, 0xCBF29CE484222325ULL);
    const std::uint64_t second = fnv1a(path, 0x84222325CBF29CE4ULL);
    for (std::size_t i = 0; i < 8; ++i) {
        key.at(i) = static_cast<std::uint8_t>(first >> (8U * i));
        key.at(8 + i) = static_cast<std::uint8_t>(second >> (8U * i));
    }
    return key;
}

Expected<std::vector<osdrop_monitor>, osdrop_result>
screen_report(std::span<const PhysicalMonitor> monitors) {
    using Result = Expected<std::vector<osdrop_monitor>, osdrop_result>;
    std::vector<osdrop_monitor> report;
    for (const PhysicalMonitor& m : monitors) {
        const std::int32_t width = m.right - m.left;
        const std::int32_t height = m.bottom - m.top;
        if (width <= 0 || height <= 0 || m.dpi == 0) {
            return Result::failure(OSDROP_ERROR_VALUE_OUT_OF_RANGE);
        }
        osdrop_monitor monitor{};
        std::ranges::copy(m.key, std::begin(monitor.key));
        monitor.native = {.x = m.left * kUnit,
                          .y = m.top * kUnit,
                          .width = width * kUnit,
                          .height = height * kUnit};
        monitor.frame = {
            .x = 0, .y = 0, .width = logical(width, m.dpi), .height = logical(height, m.dpi)};
        monitor.pixel_width = static_cast<std::uint16_t>(std::min(width, 32768));
        monitor.pixel_height = static_cast<std::uint16_t>(std::min(height, 32768));
        monitor.primary = m.primary ? 1 : 0;
        report.push_back(monitor);
    }
    if (const osdrop_result derived = osdrop_layout_derive_frames(report.data(), report.size());
        derived != OSDROP_OK) {
        return Result::failure(derived);
    }
    return report;
}

std::string device_name(std::string_view computer_name) {
    std::string name;
    std::size_t i = 0;
    while (i < computer_name.size()) {
        const auto lead = static_cast<std::uint8_t>(computer_name[i]);
        const std::size_t length = std::min(sequence_length(lead), computer_name.size() - i);
        const bool control = length == 1 && (lead < 0x20U || lead == 0x7FU);
        if (!control) {
            if (name.size() + length > kMaxNameBytes) {
                break;
            }
            name.append(computer_name.substr(i, length));
        }
        i += length;
    }
    const auto first = name.find_first_not_of(' ');
    const auto last = name.find_last_not_of(' ');
    if (first == std::string::npos) {
        return "PC";
    }
    return name.substr(first, last - first + 1);
}

} // namespace osdrop::win
