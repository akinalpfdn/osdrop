// OsdropInputTool: a developer console tool for OsdropInput (DEVPLAN Phase 3). It talks to the
// input process of this session through the input pipe, as the engine will.
//   OsdropInputTool.exe stats            the hook callbacks' timings since OsdropInput started
//   OsdropInputTool.exe capture [s]      capture this PC's input for up to s seconds (1-60,
//                                        default 10; Esc or Ctrl+Alt+Win+Esc ends it), then play
//                                        it back here at the same pace
#include <osdrop/sys/input_client.hpp>
#include <osdrop/win/input_protocol.hpp>

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

namespace {

namespace protocol = osdrop::win::input;
using Clock = std::chrono::steady_clock;

constexpr std::uint16_t kEscape = 0x01;
constexpr int kMaxSeconds = 60;
constexpr auto kStartDelay = std::chrono::seconds(5);
constexpr auto kReplayDelay = std::chrono::seconds(3);
constexpr auto kStatsTimeout = std::chrono::seconds(2);

struct Recorded {
    Clock::duration at{};
    protocol::Report report;
};

enum class CaptureState : std::uint8_t {
    Going,
    Ended,  ///< Escape, or OsdropInput ended it (the return chord, a missed heartbeat)
    Closed, ///< OsdropInput went away
};

/// What the pipe brought in, for the main thread.
class Inbox {
public:
    void add(const protocol::Report& report) {
        const std::scoped_lock lock(mutex_);
        const auto* key = std::get_if<protocol::PhysicalKey>(&report);
        const bool escape = key != nullptr && key->scan.code == kEscape && !key->scan.extended;
        if (escape || std::holds_alternative<protocol::Released>(report)) {
            ended_ = true;
        } else if (!ended_) {
            recorded_.push_back({.at = Clock::now() - started_, .report = report});
        }
        if (const auto* stats = std::get_if<protocol::Stats>(&report)) {
            stats_ = *stats;
        }
        changed_.notify_all();
    }
    void close() {
        const std::scoped_lock lock(mutex_);
        closed_ = true;
        changed_.notify_all();
    }
    void start() {
        const std::scoped_lock lock(mutex_);
        started_ = Clock::now();
    }
    /// Waits until `until`, the capture's end or the pipe closing.
    CaptureState wait_capture(Clock::time_point until) {
        std::unique_lock lock(mutex_);
        changed_.wait_until(lock, until, [this] { return ended_ || closed_; });
        if (closed_) {
            return CaptureState::Closed;
        }
        return ended_ ? CaptureState::Ended : CaptureState::Going;
    }
    std::optional<protocol::Stats> wait_stats(Clock::duration timeout) {
        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, timeout, [this] { return stats_.has_value() || closed_; });
        return stats_;
    }
    std::vector<Recorded> take() {
        const std::scoped_lock lock(mutex_);
        return std::exchange(recorded_, {});
    }
    bool closed() {
        const std::scoped_lock lock(mutex_);
        return closed_;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    Clock::time_point started_ = Clock::now();
    std::vector<Recorded> recorded_;
    std::optional<protocol::Stats> stats_;
    bool ended_ = false;
    bool closed_ = false;
};

std::unique_ptr<osdrop::sys::InputClient> connect(Inbox& inbox) {
    auto client = osdrop::sys::InputClient::connect(
        [&inbox](const protocol::Report& report) { inbox.add(report); },
        [&inbox] { inbox.close(); });
    if (!client) {
        std::cerr << "Cannot reach OsdropInput in this session (error " << GetLastError()
                  << "). Is OsdropService installed and running?\n";
    }
    return client;
}

int stats() {
    Inbox inbox;
    const auto client = connect(inbox);
    if (!client || !client->send(protocol::QueryStats{})) {
        return 1;
    }
    const auto stats = inbox.wait_stats(kStatsTimeout);
    if (!stats) {
        std::cerr << "No answer from OsdropInput.\n";
        return 1;
    }
    std::cout << "Hook callbacks: " << stats->events << ", p50 " << stats->p50_us << " us, p99 "
              << stats->p99_us << " us, longest " << stats->max_us << " us\n";
    return 0;
}

struct Counts {
    int motions = 0;
    int presses = 0;
    int scrolls = 0;
    int keys = 0;
};

Counts count(const std::vector<Recorded>& recorded) {
    Counts counts;
    for (const Recorded& entry : recorded) {
        std::visit(
            [&counts](const auto& report) {
                using T = std::decay_t<decltype(report)>;
                counts.motions += std::is_same_v<T, protocol::CapturedMotion> ? 1 : 0;
                counts.presses += std::is_same_v<T, protocol::PhysicalPress> ? 1 : 0;
                counts.scrolls += std::is_same_v<T, protocol::PhysicalScroll> ? 1 : 0;
                counts.keys += std::is_same_v<T, protocol::PhysicalKey> ? 1 : 0;
            },
            entry.report);
    }
    return counts;
}

/// Plays the captured input back on this PC from `start`, at the pace it was captured.
bool replay(osdrop::sys::InputClient& client, Inbox& inbox, const std::vector<Recorded>& recorded,
            POINT start) {
    const RECT desktop{
        .left = GetSystemMetrics(SM_XVIRTUALSCREEN),
        .top = GetSystemMetrics(SM_YVIRTUALSCREEN),
        .right = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1,
        .bottom = GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1};
    POINT cursor = start;
    bool ok = client.send(protocol::BeginControl{}) &&
              client.send(protocol::Move{.x = cursor.x, .y = cursor.y});
    const Clock::time_point began = Clock::now();
    for (const Recorded& entry : recorded) {
        if (!ok || inbox.closed()) {
            return false;
        }
        std::this_thread::sleep_until(began + entry.at);
        if (const auto* motion = std::get_if<protocol::CapturedMotion>(&entry.report)) {
            cursor.x = std::clamp(cursor.x + motion->dx, desktop.left, desktop.right);
            cursor.y = std::clamp(cursor.y + motion->dy, desktop.top, desktop.bottom);
            ok = client.send(protocol::Move{.x = cursor.x, .y = cursor.y});
        } else if (const auto* press = std::get_if<protocol::PhysicalPress>(&entry.report)) {
            ok = client.send(protocol::Press{.button = press->button, .down = press->down});
        } else if (const auto* scroll = std::get_if<protocol::PhysicalScroll>(&entry.report)) {
            ok = client.send(
                protocol::Scroll{.vertical = scroll->vertical, .horizontal = scroll->horizontal});
        } else if (const auto* key = std::get_if<protocol::PhysicalKey>(&entry.report)) {
            ok = client.send(protocol::Key{.scan = key->scan, .down = key->down});
        }
    }
    // Releases whatever the recording left down.
    return client.send(protocol::EndControl{}) && ok;
}

/// Confirms the capture until `deadline` or its end; false when OsdropInput went away.
bool wait_capture(osdrop::sys::InputClient& client, Inbox& inbox, Clock::time_point deadline) {
    while (true) {
        const auto until = std::min(deadline, Clock::now() + protocol::kHeartbeatInterval);
        const CaptureState state = inbox.wait_capture(until);
        if (state != CaptureState::Going) {
            return state == CaptureState::Ended;
        }
        if (Clock::now() >= deadline) {
            return true;
        }
        if (!client.send(protocol::Heartbeat{})) {
            return false;
        }
    }
}

int capture(int seconds) {
    Inbox inbox;
    const auto client = connect(inbox);
    if (!client) {
        return 1;
    }
    POINT before{};
    GetCursorPos(&before);
    const POINT park{.x = GetSystemMetrics(SM_CXSCREEN) / 2,
                     .y = GetSystemMetrics(SM_CYSCREEN) / 2};
    std::cout << "The capture starts in 5 s: click into the window to try it on.\n";
    std::this_thread::sleep_for(kStartDelay);
    std::cout << "Capturing for " << seconds
              << " s: this PC's mouse and keyboard act nowhere until then. Esc ends it early.\n";
    inbox.start();
    if (!client->send(protocol::StartCapture{
            .park_x = park.x, .park_y = park.y, .home_x = before.x, .home_y = before.y})) {
        return 1;
    }
    const bool intact = wait_capture(*client, inbox, Clock::now() + std::chrono::seconds(seconds));
    static_cast<void>(client->send(protocol::StopCapture{.x = before.x, .y = before.y}));
    if (!intact) {
        // OsdropInput went while the cursors were hidden: this user's own scheme comes back.
        SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
        std::cerr << "OsdropInput went away during the capture.\n";
        return 1;
    }
    const std::vector<Recorded> recorded = inbox.take();
    const Counts counts = count(recorded);
    std::cout << "Captured " << counts.motions << " movements, " << counts.presses
              << " button presses and releases, " << counts.scrolls << " scrolls and "
              << counts.keys << " key presses and releases.\n"
              << "Playing them back in 3 s from the middle of the main screen...\n";
    std::this_thread::sleep_for(kReplayDelay);
    if (!replay(*client, inbox, recorded, park)) {
        std::cerr << "The playback stopped: OsdropInput went away or refused it.\n";
        return 1;
    }
    std::cout << "Done.\n";
    return 0;
}

int usage() {
    std::cerr << "Usage: OsdropInputTool.exe stats\n"
                 "       OsdropInputTool.exe capture [seconds 1-60, default 10]\n";
    return 2;
}

std::optional<int> seconds_of(std::wstring_view text) {
    std::string narrow;
    for (const wchar_t c : text) {
        narrow.push_back(static_cast<char>(c));
    }
    int value = 0;
    const auto [end, error] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), value);
    if (error != std::errc() || end != narrow.data() + narrow.size() || value < 1 ||
        value > kMaxSeconds) {
        return std::nullopt;
    }
    return value;
}

} // namespace

// Entry point: the signature is fixed by the C runtime.
// NOLINTNEXTLINE(misc-use-internal-linkage,*-avoid-c-arrays)
int wmain(int argc, wchar_t* argv[]) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring_view command = argc >= 2 ? std::wstring_view(argv[1]) : std::wstring_view();
    if (command == L"stats" && argc == 2) {
        return stats();
    }
    if (command == L"capture" && argc <= 3) {
        const auto seconds = argc == 3 ? seconds_of(argv[2]) : std::optional(10);
        return seconds ? capture(*seconds) : usage();
    }
    return usage();
}
