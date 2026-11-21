#include "hooks.hpp"

#include <osdrop/win/key_codes.hpp>

#include <algorithm>
#include <initializer_list>

namespace osdrop::input {
namespace {

namespace protocol = win::input;

// The one Hooks of this process: hook callbacks are plain functions.
std::atomic<Hooks*> g_hooks{nullptr}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

constexpr std::int64_t kLocalInputIntervalMs = 100;
/// After Osdrop moves the cursor, movement is not reported for this long (1/500 s).
constexpr std::int64_t kWarpSettleDivisor = 500;

/// Tells the hook thread that new chords wait (set_chords).
constexpr UINT kChordsMessage = WM_APP + 1;

bool injected_by_osdrop(DWORD flags, DWORD injected_flag, ULONG_PTR extra) {
    return (flags & injected_flag) != 0 && extra == kInjectedTag;
}

} // namespace

Hooks::Hooks(protocol::ReportQueue& reports) : reports_(reports) {
    QueryPerformanceFrequency(&frequency_);
}

Hooks::~Hooks() {
    if (const DWORD id = thread_id_.load(); id != 0) {
        PostThreadMessageW(id, WM_QUIT, 0, 0);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    g_hooks = nullptr;
}

bool Hooks::start() {
    g_hooks = this;
    thread_ = std::thread([this] { run(); });
    installed_.wait(-1);
    return installed_.load() == 1;
}

void Hooks::run() {
    // Callbacks run on this thread; it must answer them at once, whatever else the PC does.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // creates the message queue
    thread_id_ = GetCurrentThreadId();
    auto* const module = GetModuleHandleW(nullptr);
    HHOOK mouse = SetWindowsHookExW(WH_MOUSE_LL, on_mouse, module, 0);
    HHOOK keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, on_keyboard, module, 0);
    installed_ = mouse != nullptr && keyboard != nullptr ? 1 : 0;
    installed_.notify_all();
    apply_chords(); // set before the thread had a queue
    if (installed_.load() == 1) {
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (message.hwnd == nullptr && message.message == kChordsMessage) {
                apply_chords();
                continue;
            }
            DispatchMessageW(&message);
        }
    }
    for (HHOOK hook : {mouse, keyboard}) {
        if (hook != nullptr) {
            UnhookWindowsHookEx(hook);
        }
    }
}

void Hooks::capture() {
    capturing_ = true;
}

void Hooks::release() {
    capturing_ = false;
}

void Hooks::set_controlled(bool controlled) {
    controlled_ = controlled;
}

void Hooks::set_watching(bool watching) {
    watching_ = watching;
}

void Hooks::set_chords(const protocol::Chord& return_home, const protocol::Chord& lock_input) {
    {
        const std::scoped_lock lock(chords_mutex_);
        new_chords_ = std::pair(return_home, lock_input);
    }
    if (const DWORD id = thread_id_.load(); id != 0) {
        PostThreadMessageW(id, kChordsMessage, 0, 0);
    }
}

void Hooks::apply_chords() {
    std::optional<std::pair<protocol::Chord, protocol::Chord>> chords;
    {
        const std::scoped_lock lock(chords_mutex_);
        chords = std::exchange(new_chords_, std::nullopt);
    }
    if (chords) {
        chords_.set(chords->first, chords->second);
    }
}

void Hooks::warping() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    warped_ = now.QuadPart;
}

LRESULT CALLBACK Hooks::on_mouse(int code, WPARAM message, LPARAM data) {
    Hooks* hooks = g_hooks.load(std::memory_order_acquire);
    if (code == HC_ACTION && hooks != nullptr) {
        LARGE_INTEGER started{};
        QueryPerformanceCounter(&started);
        // NOLINTNEXTLINE(performance-no-int-to-ptr): the hook's data is a pointer in an LPARAM
        const auto& event = *reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
        const bool swallow = hooks->mouse(message, event, started.QuadPart);
        hooks->record(started);
        if (swallow) {
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}

LRESULT CALLBACK Hooks::on_keyboard(int code, WPARAM message, LPARAM data) {
    Hooks* hooks = g_hooks.load(std::memory_order_acquire);
    if (code == HC_ACTION && hooks != nullptr) {
        LARGE_INTEGER started{};
        QueryPerformanceCounter(&started);
        // NOLINTNEXTLINE(performance-no-int-to-ptr): the hook's data is a pointer in an LPARAM
        const bool swallow = hooks->keyboard(*reinterpret_cast<const KBDLLHOOKSTRUCT*>(data));
        hooks->record(started);
        if (swallow) {
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}

bool Hooks::mouse(WPARAM message, const MSLLHOOKSTRUCT& event, std::int64_t ticks) {
    if (injected_by_osdrop(event.flags, LLMHF_INJECTED, event.dwExtraInfo)) {
        return false;
    }
    if ((event.flags & LLMHF_INJECTED) == 0 &&
        (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)) {
        left_held_ = message == WM_LBUTTONDOWN; // before Windows acts on it
    }
    if (capturing_.load(std::memory_order_relaxed)) {
        report(message, event, ticks, true);
        return true;
    }
    if (controlled_.load(std::memory_order_relaxed)) {
        if ((event.flags & LLMHF_INJECTED) == 0) {
            local_input();
        }
    } else if (watching_.load(std::memory_order_relaxed)) {
        report(message, event, ticks, false);
    }
    return false;
}

void Hooks::report(WPARAM message, const MSLLHOOKSTRUCT& event, std::int64_t ticks, bool captured) {
    const auto press = [&](protocol::Button button, bool down) {
        reports_.push(protocol::PhysicalPress{.button = button, .down = down});
    };
    // The wheel's delta is the high word of mouseData, signed.
    const auto wheel = static_cast<std::int16_t>(HIWORD(event.mouseData));
    const bool first_x = HIWORD(event.mouseData) == XBUTTON1;
    switch (message) {
    case WM_MOUSEMOVE:
        report_motion(event.pt, ticks, captured);
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        press(protocol::Button::Left, message == WM_LBUTTONDOWN);
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        press(protocol::Button::Right, message == WM_RBUTTONDOWN);
        break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        press(protocol::Button::Middle, message == WM_MBUTTONDOWN);
        break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        press(first_x ? protocol::Button::X1 : protocol::Button::X2, message == WM_XBUTTONDOWN);
        break;
    case WM_MOUSEWHEEL:
        reports_.push(protocol::PhysicalScroll{.vertical = wheel, .horizontal = 0});
        break;
    case WM_MOUSEHWHEEL:
        reports_.push(protocol::PhysicalScroll{.vertical = 0, .horizontal = wheel});
        break;
    default:
        break;
    }
}

void Hooks::report_motion(POINT to, std::int64_t ticks, bool captured) {
    // The cursor has not moved yet: Windows moves it once the hooks let the event through. A
    // swallowed move leaves it where it is, so captured movement never runs out at an edge.
    POINT from{};
    if (ticks - warped_.load(std::memory_order_relaxed) <
            frequency_.QuadPart / kWarpSettleDivisor ||
        GetCursorPos(&from) == FALSE) {
        return; // measured from before Osdrop moved the cursor, or another desktop's
    }
    const std::int32_t dx = to.x - from.x;
    const std::int32_t dy = to.y - from.y;
    if (dx == 0 && dy == 0) {
        return;
    }
    const auto time = static_cast<std::uint64_t>(ticks);
    if (captured) {
        reports_.push(protocol::CapturedMotion{.dx = dx, .dy = dy, .ticks = time});
    } else {
        reports_.push(
            protocol::WatchedMotion{.x = to.x, .y = to.y, .dx = dx, .dy = dy, .ticks = time});
    }
}

bool Hooks::keyboard(const KBDLLHOOKSTRUCT& event) {
    if (injected_by_osdrop(event.flags, LLKHF_INJECTED, event.dwExtraInfo)) {
        return false;
    }
    const win::ScanCode scan{.code = static_cast<std::uint16_t>(event.scanCode),
                             .extended = (event.flags & LLKHF_EXTENDED) != 0};
    const bool down = (event.flags & LLKHF_UP) == 0;
    const bool capturing = capturing_.load(std::memory_order_relaxed);
    if (chord(scan, down, capturing)) {
        return true;
    }
    // Keys without a place in the table are not reported (and swallowed while captured).
    const bool known = win::usage_of(scan).has_value();
    if (capturing) {
        if (known) {
            reports_.push(protocol::PhysicalKey{.scan = scan, .down = down});
        }
        return true;
    }
    if (controlled_.load(std::memory_order_relaxed)) {
        if ((event.flags & LLKHF_INJECTED) == 0) {
            local_input();
        }
    } else if (known && watching_.load(std::memory_order_relaxed)) {
        reports_.push(protocol::PhysicalKey{.scan = scan, .down = down});
    }
    return false;
}

bool Hooks::chord(win::ScanCode scan, bool down, bool capturing) {
    // The chords are Osdrop's while it watches or captures this PC's input; while another
    // computer controls this PC, its keyboard takes control back instead.
    const bool active = capturing || (watching_.load(std::memory_order_relaxed) &&
                                      !controlled_.load(std::memory_order_relaxed));
    using Outcome = protocol::ChordMatcher::Outcome;
    switch (chords_.key(scan, down, active)) {
    case Outcome::Pass:
        return false;
    case Outcome::Swallow:
        return true;
    case Outcome::ReturnHome:
        if (capturing) {
            // Input acts here again at once, whatever the client does; the pipe thread shows the
            // cursor.
            capturing_ = false;
            reports_.push(protocol::Released{.reason = protocol::ReleaseReason::ReturnChord});
        } else {
            reports_.push(protocol::HotkeyPressed{.hotkey = protocol::Hotkey::ReturnHome});
        }
        return true;
    case Outcome::LockInput:
        reports_.push(protocol::HotkeyPressed{.hotkey = protocol::Hotkey::LockInput});
        return true;
    }
    return false;
}

void Hooks::local_input() {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    std::int64_t last = last_local_ms_.load(std::memory_order_relaxed);
    if (now - last >= kLocalInputIntervalMs &&
        last_local_ms_.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
        reports_.push(protocol::LocalInput{});
    }
}

void Hooks::record(LARGE_INTEGER started) {
    LARGE_INTEGER ended{};
    QueryPerformanceCounter(&ended);
    const std::int64_t ticks = ended.QuadPart - started.QuadPart;
    const std::int64_t micros =
        frequency_.QuadPart > 0 ? ticks * 1'000'000 / frequency_.QuadPart : 0;
    timings_.record(static_cast<std::uint32_t>(std::min<std::int64_t>(micros, UINT32_MAX)));
}

} // namespace osdrop::input
