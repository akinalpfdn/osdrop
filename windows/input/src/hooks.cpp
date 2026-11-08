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

std::int64_t pack(POINT point) {
    return (static_cast<std::int64_t>(point.x) << 32) | static_cast<std::uint32_t>(point.y);
}

POINT unpack(std::int64_t packed) {
    return {.x = static_cast<LONG>(packed >> 32),
            .y = static_cast<LONG>(static_cast<std::int32_t>(packed))};
}

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
    if (installed_.load() == 1) {
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            DispatchMessageW(&message);
        }
    }
    for (HHOOK hook : {mouse, keyboard}) {
        if (hook != nullptr) {
            UnhookWindowsHookEx(hook);
        }
    }
}

void Hooks::capture(POINT park) {
    park_ = pack(park);
    capturing_ = true;
}

void Hooks::release() {
    capturing_ = false;
}

void Hooks::set_controlled(bool controlled) {
    controlled_ = controlled;
}

LRESULT CALLBACK Hooks::on_mouse(int code, WPARAM message, LPARAM data) {
    Hooks* hooks = g_hooks.load(std::memory_order_acquire);
    if (code == HC_ACTION && hooks != nullptr) {
        LARGE_INTEGER started{};
        QueryPerformanceCounter(&started);
        // NOLINTNEXTLINE(performance-no-int-to-ptr): the hook's data is a pointer in an LPARAM
        const bool swallow = hooks->mouse(message, *reinterpret_cast<const MSLLHOOKSTRUCT*>(data));
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

bool Hooks::mouse(WPARAM message, const MSLLHOOKSTRUCT& event) {
    if (injected_by_osdrop(event.flags, LLMHF_INJECTED, event.dwExtraInfo)) {
        return false;
    }
    if (!capturing_.load(std::memory_order_relaxed)) {
        if (controlled_.load(std::memory_order_relaxed) && (event.flags & LLMHF_INJECTED) == 0) {
            local_input();
        }
        return false;
    }
    const auto press = [&](protocol::Button button, bool down) {
        reports_.push(protocol::CapturedPress{.button = button, .down = down});
    };
    // The wheel's delta is the high word of mouseData, signed.
    const auto wheel = static_cast<std::int16_t>(HIWORD(event.mouseData));
    const bool first_x = HIWORD(event.mouseData) == XBUTTON1;
    switch (message) {
    case WM_MOUSEMOVE: {
        // Swallowed moves leave the cursor at the park point; the event says where it would go.
        const POINT park = unpack(park_.load(std::memory_order_relaxed));
        const std::int32_t dx = event.pt.x - park.x;
        const std::int32_t dy = event.pt.y - park.y;
        if (dx != 0 || dy != 0) {
            reports_.push(protocol::CapturedMotion{.dx = dx, .dy = dy});
        }
        break;
    }
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
        reports_.push(protocol::CapturedScroll{.vertical = wheel, .horizontal = 0});
        break;
    case WM_MOUSEHWHEEL:
        reports_.push(protocol::CapturedScroll{.vertical = 0, .horizontal = wheel});
        break;
    default:
        break;
    }
    return true;
}

bool Hooks::keyboard(const KBDLLHOOKSTRUCT& event) {
    if (injected_by_osdrop(event.flags, LLKHF_INJECTED, event.dwExtraInfo)) {
        return false;
    }
    if (!capturing_.load(std::memory_order_relaxed)) {
        if (controlled_.load(std::memory_order_relaxed) && (event.flags & LLKHF_INJECTED) == 0) {
            local_input();
        }
        return false;
    }
    const win::ScanCode scan{.code = static_cast<std::uint16_t>(event.scanCode),
                             .extended = (event.flags & LLKHF_EXTENDED) != 0};
    if (win::usage_of(scan)) { // keys without a place in the table are swallowed, not sent
        reports_.push(protocol::CapturedKey{.scan = scan, .down = (event.flags & LLKHF_UP) == 0});
    }
    return true;
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
