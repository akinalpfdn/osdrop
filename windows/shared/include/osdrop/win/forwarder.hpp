#pragma once

// This PC's own input as core takes it (DEVPLAN Phase 11): OsdropInput's reports of the mouse and
// keyboard, watched or captured, as osdrop_input events for osdrop_mesh_input. Positions are 1/16
// of a physical pixel. A watched cursor is kept on a monitor as Windows keeps it, while its
// movement still pushes past an outer edge; a captured one waits at the park point. Clicks are
// counted with this PC's double-click settings, by where the cursor is on the computer the input
// goes to, so quick clicks on different things there stay single clicks; a key that goes down
// again while down is a repeat. Portable and unit tested.

#include <osdrop/osdrop.h>
#include <osdrop/win/input_protocol.hpp>
#include <osdrop/win/screens.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace osdrop::win {

struct Pixel {
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool operator==(const Pixel&) const = default;
};

/// GetDoubleClickTime and SM_CXDOUBLECLK, SM_CYDOUBLECLK: a press continues a double click within
/// the interval and the rectangle centred on the previous press.
struct ClickSettings {
    std::chrono::milliseconds interval{500};
    std::int32_t width = 4;
    std::int32_t height = 4;
};

/// One input for core; `ticks` is QueryPerformanceCounter when the hook saw it, 0 when not known.
struct Forwarded {
    osdrop_input input;
    std::uint64_t ticks = 0;
};

class InputForwarder {
public:
    using Clock = std::chrono::steady_clock;

    explicit InputForwarder(ClickSettings clicks);

    /// This PC's monitors, which a watched cursor stays on.
    void set_monitors(std::vector<PhysicalMonitor> monitors);
    /// Capture began: the cursor waits at `park` until release(), which shows it at `at`. A double
    /// click never spans the two.
    void capture(Pixel park);
    void release(Pixel at);
    /// Where the cursor is, as the last report put it.
    [[nodiscard]] Pixel position() const { return position_; }

    /// The event for a report of this PC's input; nullopt for other reports and for what carries
    /// nothing (a key no table has).
    [[nodiscard]] std::optional<Forwarded> convert(const input::Report& report,
                                                   Clock::time_point now);

    /// Held keys and click counts are forgotten: OsdropInput was reached again.
    void reset();

private:
    struct LastPress {
        input::Button button = input::Button::Left;
        Pixel at;
        Clock::time_point time;
        std::uint8_t count = 0;
    };
    static constexpr std::size_t kButtons = 5;

    [[nodiscard]] Pixel on_monitor(Pixel point) const;
    [[nodiscard]] osdrop_input pointer(std::int32_t dx, std::int32_t dy) const;
    [[nodiscard]] osdrop_input button(const input::PhysicalPress& press, Clock::time_point now);
    [[nodiscard]] std::optional<osdrop_input> key(const input::PhysicalKey& pressed);
    [[nodiscard]] osdrop_input scroll(const input::PhysicalScroll& scroll) const;

    ClickSettings clicks_;
    std::vector<PhysicalMonitor> monitors_;
    Pixel position_;
    bool captured_ = false;
    Pixel travelled_; ///< while captured: the movement since, for counting clicks
    std::optional<LastPress> last_press_;
    std::array<std::uint8_t, kButtons> press_counts_{}; ///< each button's count when it went down
    std::set<ScanCode> keys_down_;
};

} // namespace osdrop::win
