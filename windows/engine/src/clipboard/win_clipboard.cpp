#include "clipboard/win_clipboard.hpp"

#include "clipboard/wic_images.hpp"

#include <osdrop/win/clipboard_text.hpp>

#include <windows.h>

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <filesystem>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace osdrop::engine {
namespace {

namespace fs = std::filesystem;

constexpr UINT kRunPosted = WM_APP + 1;
constexpr UINT kStop = WM_APP + 2;
constexpr const wchar_t* kWindowClass = L"OsdropClipboard";
constexpr int kOpenAttempts = 10;
constexpr DWORD kOpenRetryMs = 20;
constexpr DWORD kDropEffectCopy = 1; // DROPEFFECT_COPY: Explorer copies rather than moves
constexpr int kGuidChars = 39;

/// Formats registered by name, the same in every process.
struct Formats {
    UINT rtf = RegisterClipboardFormatW(L"Rich Text Format");
    UINT html = RegisterClipboardFormatW(L"HTML Format");
    UINT png = RegisterClipboardFormatW(L"PNG");
    UINT drop_effect = RegisterClipboardFormatW(L"Preferred DropEffect");
    // Set by password managers and other applications on data that must not be shared.
    UINT exclude = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    UINT viewer_ignore = RegisterClipboardFormatW(L"Clipboard Viewer Ignore");
    UINT history = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    UINT cloud = RegisterClipboardFormatW(L"CanUploadToCloudClipboard");
};

const Formats& formats() {
    static const Formats registered;
    return registered;
}

/// Closes the clipboard it opened, retrying while another application holds it.
class OpenClipboardScope {
public:
    explicit OpenClipboardScope(HWND window) {
        for (int attempt = 0; attempt < kOpenAttempts && !open_; ++attempt) {
            open_ = OpenClipboard(window) != FALSE;
            if (!open_) {
                Sleep(kOpenRetryMs);
            }
        }
    }
    ~OpenClipboardScope() {
        if (open_) {
            CloseClipboard();
        }
    }
    OpenClipboardScope(const OpenClipboardScope&) = delete;
    OpenClipboardScope& operator=(const OpenClipboardScope&) = delete;
    OpenClipboardScope(OpenClipboardScope&&) = delete;
    OpenClipboardScope& operator=(OpenClipboardScope&&) = delete;

    [[nodiscard]] bool is_open() const { return open_; }

private:
    bool open_ = false;
};

/// The bytes of one format on the open clipboard.
std::optional<std::vector<std::uint8_t>> read_format(UINT format) {
    HANDLE handle = GetClipboardData(format);
    if (handle == nullptr) {
        return std::nullopt;
    }
    const void* data = GlobalLock(handle);
    if (data == nullptr) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(GlobalSize(handle));
    std::memcpy(bytes.data(), data, bytes.size());
    GlobalUnlock(handle);
    return bytes;
}

/// A DWORD format set to 0: the application keeps the copy out of history or the cloud.
bool refuses(UINT format) {
    const auto value = read_format(format);
    return value && value->size() >= sizeof(DWORD) &&
           std::all_of(value->begin(), value->begin() + sizeof(DWORD),
                       [](std::uint8_t byte) { return byte == 0; });
}

bool concealed() {
    const Formats& registered = formats();
    return IsClipboardFormatAvailable(registered.exclude) != FALSE ||
           IsClipboardFormatAvailable(registered.viewer_ignore) != FALSE ||
           refuses(registered.history) || refuses(registered.cloud);
}

std::vector<std::uint8_t> without_trailing_nuls(std::vector<std::uint8_t> bytes) {
    while (!bytes.empty() && bytes.back() == 0) {
        bytes.pop_back();
    }
    return bytes;
}

std::vector<std::uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

std::optional<std::string> utf8_of_wide(std::wstring_view text) {
    return win::utf8_of_utf16(
        std::u16string_view(reinterpret_cast<const char16_t*>(text.data()), text.size()));
}

std::optional<std::string> text_of(const std::vector<std::uint8_t>& units) {
    return win::utf8_of_utf16(std::u16string_view(reinterpret_cast<const char16_t*>(units.data()),
                                                  units.size() / sizeof(char16_t)));
}

std::vector<std::string> dropped_files() {
    auto* drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    if (drop == nullptr) {
        return {};
    }
    std::vector<std::string> files;
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i) {
        const UINT length = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(length + 1, L'\0');
        DragQueryFileW(drop, i, path.data(), length + 1);
        path.resize(length);
        if (auto utf8 = utf8_of_wide(path)) {
            files.push_back(std::move(*utf8));
        }
    }
    return files;
}

/// Hands `bytes` to the open clipboard as `format`; the clipboard owns the memory afterwards.
void set_format(UINT format, std::span<const std::uint8_t> bytes) {
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, std::max<std::size_t>(bytes.size(), 1));
    if (global == nullptr) {
        return;
    }
    void* data = GlobalLock(global);
    if (data == nullptr) {
        GlobalFree(global);
        return;
    }
    std::memcpy(data, bytes.data(), bytes.size());
    GlobalUnlock(global);
    if (SetClipboardData(format, global) == nullptr) {
        GlobalFree(global);
    }
}

void set_text(UINT format, std::string_view utf8) {
    auto wide = win::utf16_of_utf8(utf8);
    if (!wide) {
        return;
    }
    wide->push_back(u'\0');
    const auto* units = reinterpret_cast<const std::uint8_t*>(wide->data());
    set_format(format, std::span(units, wide->size() * sizeof(char16_t)));
}

/// CF_HDROP: a DROPFILES header, then each path in UTF-16 with a NUL, then another NUL.
std::vector<std::uint8_t> drop_files_of(const std::vector<std::string>& files) {
    const DROPFILES header{
        .pFiles = sizeof(DROPFILES), .pt = {.x = 0, .y = 0}, .fNC = FALSE, .fWide = TRUE};
    std::u16string list;
    for (const std::string& file : files) {
        if (const auto wide = win::utf16_of_utf8(file)) {
            list += *wide;
            list.push_back(u'\0');
        }
    }
    list.push_back(u'\0');
    std::vector<std::uint8_t> bytes(sizeof(DROPFILES) + (list.size() * sizeof(char16_t)));
    std::memcpy(bytes.data(), &header, sizeof(DROPFILES));
    std::memcpy(bytes.data() + sizeof(DROPFILES), list.data(), list.size() * sizeof(char16_t));
    return bytes;
}

fs::path staging_root() {
    std::array<wchar_t, MAX_PATH + 1> temp{};
    const DWORD length = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
    return fs::path(std::wstring(temp.data(), length)) / L"Osdrop" / L"clipboard";
}

fs::path path_of(const std::string& utf8) {
    return {std::u8string(utf8.begin(), utf8.end())};
}

} // namespace

WinClipboard::WinClipboard() : thread_([this] { run(); }) {
    std::unique_lock lock(mutex_);
    started_.wait(lock, [this] { return ready_; });
}

WinClipboard::~WinClipboard() {
    try {
        stop();
    } catch (...) {
        // Only locking and joining can throw; a thread that cannot be stopped is unrecoverable,
        // and this makes the implicit terminate explicit.
        std::terminate();
    }
}

void WinClipboard::listen(std::function<void(win::ClipData)> copied) {
    post([this, copied = std::move(copied)]() mutable { copied_ = std::move(copied); });
}

void WinClipboard::stop() {
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
        queue_.clear();
    }
    if (HWND window = window_.load(); window != nullptr) {
        PostMessageW(window, kStop, 0, 0);
    }
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) {
        thread_.join();
    }
}

void WinClipboard::post(std::function<void()> work) {
    {
        const std::scoped_lock lock(mutex_);
        if (stopping_) {
            return;
        }
        queue_.push_back(std::move(work));
    }
    if (HWND window = window_.load(); window != nullptr) {
        PostMessageW(window, kRunPosted, 0, 0);
    }
}

void WinClipboard::write(const win::ClipData& data) {
    const Formats& registered = formats();
    std::optional<std::vector<std::uint8_t>> dib;
    for (const win::ClipContent& content : data.contents) {
        if (content.format == OSDROP_CLIPBOARD_IMAGE) {
            dib = dib_of_png(content.bytes); // before the clipboard is opened: it can take a while
        }
    }
    const OpenClipboardScope open(window_.load());
    if (!open.is_open()) {
        return;
    }
    EmptyClipboard(); // makes this window the owner, so its own change is not reported back
    for (const win::ClipContent& content : data.contents) {
        const std::string_view text(reinterpret_cast<const char*>(content.bytes.data()),
                                    content.bytes.size());
        switch (content.format) {
        case OSDROP_CLIPBOARD_TEXT:
            set_text(CF_UNICODETEXT, text);
            break;
        case OSDROP_CLIPBOARD_RTF: {
            std::vector<std::uint8_t> rtf = content.bytes;
            rtf.push_back(0);
            set_format(registered.rtf, rtf);
            break;
        }
        case OSDROP_CLIPBOARD_HTML: {
            std::vector<std::uint8_t> block = win::cf_html_of_html(text);
            block.push_back(0);
            set_format(registered.html, block);
            break;
        }
        case OSDROP_CLIPBOARD_IMAGE:
            set_format(registered.png, content.bytes);
            if (dib) {
                set_format(CF_DIB, *dib);
            }
            break;
        default:
            break;
        }
    }
    if (!data.files.empty()) {
        set_format(CF_HDROP, drop_files_of(data.files));
        const DWORD effect = kDropEffectCopy;
        set_format(registered.drop_effect,
                   std::span(reinterpret_cast<const std::uint8_t*>(&effect), sizeof(DWORD)));
    }
}

std::optional<std::string> WinClipboard::make_staging_folder() {
    GUID guid{};
    std::array<wchar_t, kGuidChars + 1> name{};
    if (FAILED(CoCreateGuid(&guid)) ||
        StringFromGUID2(guid, name.data(), static_cast<int>(name.size())) == 0) {
        return std::nullopt;
    }
    const fs::path folder = staging_root() / name.data();
    std::error_code error;
    fs::create_directories(folder, error);
    if (error) {
        return std::nullopt;
    }
    return utf8_of_wide(folder.wstring());
}

void WinClipboard::clear_staging(const std::optional<std::string>& kept) {
    const fs::path root = staging_root();
    const std::optional<fs::path> keep = kept ? std::optional(path_of(*kept)) : std::nullopt;
    std::error_code error;
    for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        if (keep && it->path() == *keep) {
            continue;
        }
        std::error_code ignored;
        fs::remove_all(it->path(), ignored);
    }
}

LRESULT CALLBACK WinClipboard::window_proc(HWND window, UINT message, WPARAM wparam,
                                           LPARAM lparam) {
    // Win32 passes the window's object as integers: the creation parameter, then user data.
    if (message == WM_NCCREATE) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr): LPARAM of WM_NCCREATE
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    // NOLINTNEXTLINE(performance-no-int-to-ptr): the pointer stored above
    auto* self = reinterpret_cast<WinClipboard*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self != nullptr) {
        switch (message) {
        case WM_CLIPBOARDUPDATE:
            self->changed();
            return 0;
        case kRunPosted:
            self->run_posted();
            return 0;
        case kStop:
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void WinClipboard::run() {
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    clear_staging(std::nullopt); // leftovers from an earlier run
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const WNDCLASSEXW window_class{.cbSize = sizeof(WNDCLASSEXW),
                                   .style = 0,
                                   .lpfnWndProc = window_proc,
                                   .cbClsExtra = 0,
                                   .cbWndExtra = 0,
                                   .hInstance = instance,
                                   .hIcon = nullptr,
                                   .hCursor = nullptr,
                                   .hbrBackground = nullptr,
                                   .lpszMenuName = nullptr,
                                   .lpszClassName = kWindowClass,
                                   .hIconSm = nullptr};
    RegisterClassExW(&window_class); // already registered by an earlier engine: fine
    HWND window =
        CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
    if (window != nullptr) {
        AddClipboardFormatListener(window);
    }
    window_ = window;
    {
        const std::scoped_lock lock(mutex_);
        ready_ = true;
    }
    started_.notify_all();
    if (window != nullptr) {
        PostMessageW(window, kRunPosted, 0, 0); // work posted before the window existed
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            DispatchMessageW(&message);
        }
        RemoveClipboardFormatListener(window);
        window_ = nullptr;
        DestroyWindow(window);
    }
    if (com) {
        CoUninitialize();
    }
}

void WinClipboard::run_posted() {
    for (;;) {
        std::function<void()> work;
        {
            const std::scoped_lock lock(mutex_);
            if (stopping_ || queue_.empty()) {
                return;
            }
            work = std::move(queue_.front());
            queue_.pop_front();
        }
        work();
    }
}

void WinClipboard::changed() {
    HWND window = window_.load();
    if (!copied_ || GetClipboardOwner() == window) {
        return; // nobody listens yet, or this is Osdrop's own write
    }
    const Formats& registered = formats();
    win::ClipData data;
    std::optional<std::vector<std::uint8_t>> dib;
    {
        const OpenClipboardScope open(window);
        if (!open.is_open() || concealed()) {
            return;
        }
        if (const auto units = read_format(CF_UNICODETEXT)) {
            if (auto text = text_of(*units)) {
                data.contents.push_back(
                    {.format = OSDROP_CLIPBOARD_TEXT, .bytes = bytes_of(*text)});
            }
        }
        if (auto rtf = read_format(registered.rtf)) {
            data.contents.push_back(
                {.format = OSDROP_CLIPBOARD_RTF, .bytes = without_trailing_nuls(std::move(*rtf))});
        }
        if (const auto block = read_format(registered.html)) {
            if (const auto html = win::html_of_cf_html(*block)) {
                data.contents.push_back(
                    {.format = OSDROP_CLIPBOARD_HTML, .bytes = bytes_of(*html)});
            }
        }
        if (auto png = read_format(registered.png)) {
            data.contents.push_back({.format = OSDROP_CLIPBOARD_IMAGE, .bytes = std::move(*png)});
        } else {
            dib = read_format(CF_DIBV5);
            if (!dib) {
                dib = read_format(CF_DIB);
            }
        }
        if (IsClipboardFormatAvailable(CF_HDROP) != FALSE) {
            data.files = dropped_files();
        }
    }
    if (dib) { // converted once the clipboard is closed again
        if (auto png = png_of_dib(*dib)) {
            data.contents.push_back({.format = OSDROP_CLIPBOARD_IMAGE, .bytes = std::move(*png)});
        }
    }
    copied_(std::move(data));
}

} // namespace osdrop::engine
