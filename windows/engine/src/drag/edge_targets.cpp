#include "drag/edge_targets.hpp"

#include "drag/com_object.hpp"

#include <windows.h>

#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace osdrop::engine::drag {
namespace {

// STGMEDIUM is OLE's tagged union; tymed says which member is set.
// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)

constexpr UINT kPlaceMessage = WM_APP + 1;
constexpr UINT kPollMilliseconds = 30;
/// Nearly invisible, yet still hit by the drop: a fully transparent window is not.
constexpr BYTE kAlpha = 1;
constexpr const wchar_t* kClassName = L"OsdropEdgeTarget";
constexpr ULONG kCopyChunk = 64 * 1024;

FORMATETC format(CLIPFORMAT format, DWORD media, LONG index = -1) {
    return {.cfFormat = format,
            .ptd = nullptr,
            .dwAspect = DVASPECT_CONTENT,
            .lindex = index,
            .tymed = media};
}

CLIPFORMAT registered(const wchar_t* name) {
    return static_cast<CLIPFORMAT>(RegisterClipboardFormatW(name));
}

/// Owns a STGMEDIUM filled by GetData.
class Medium {
public:
    Medium() = default;
    ~Medium() { ReleaseStgMedium(&medium_); }
    Medium(const Medium&) = delete;
    Medium& operator=(const Medium&) = delete;
    Medium(Medium&&) = delete;
    Medium& operator=(Medium&&) = delete;
    STGMEDIUM* get() { return &medium_; }
    STGMEDIUM* operator->() { return &medium_; }

private:
    STGMEDIUM medium_{};
};

std::vector<std::filesystem::path> dropped_files(IDataObject* data) {
    FORMATETC wanted = format(CF_HDROP, TYMED_HGLOBAL);
    Medium medium;
    if (FAILED(data->GetData(&wanted, medium.get()))) {
        return {};
    }
    auto* drop = static_cast<HDROP>(GlobalLock(medium->hGlobal));
    std::vector<std::filesystem::path> items;
    if (drop != nullptr) {
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(length, L'\0');
            DragQueryFileW(drop, i, path.data(), length + 1);
            items.emplace_back(path);
        }
        GlobalUnlock(medium->hGlobal);
    }
    return items;
}

/// A descriptor's name as a path inside the staging folder; empty for one that would leave it.
std::filesystem::path inside(std::wstring_view name) {
    std::filesystem::path relative(name);
    if (name.empty() || relative.has_root_name() || relative.has_root_directory()) {
        return {};
    }
    for (const auto& part : relative) {
        if (part == L".." || part == L".") {
            return {};
        }
    }
    return relative;
}

bool write_stream(IStream* stream, const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    std::vector<char> buffer(kCopyChunk);
    for (;;) {
        ULONG read = 0;
        const HRESULT result = stream->Read(buffer.data(), kCopyChunk, &read);
        if (FAILED(result)) {
            return false;
        }
        if (read == 0) {
            break;
        }
        out.write(buffer.data(), static_cast<std::streamsize>(read));
    }
    return static_cast<bool>(out);
}

bool write_global(HGLOBAL global, std::uint64_t size, const std::filesystem::path& path) {
    const void* bytes = GlobalLock(global);
    if (bytes == nullptr) {
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char*>(bytes),
              static_cast<std::streamsize>(std::min<std::uint64_t>(size, GlobalSize(global))));
    GlobalUnlock(global);
    return static_cast<bool>(out);
}

bool write_storage(IStorage* storage, const std::filesystem::path& path) {
    ComPtr<IStorage> copy;
    constexpr DWORD kMode = STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE;
    if (FAILED(StgCreateStorageEx(path.c_str(), kMode, STGFMT_STORAGE, 0, nullptr, nullptr,
                                  IID_PPV_ARGS(copy.put())))) {
        return false;
    }
    return SUCCEEDED(storage->CopyTo(0, nullptr, nullptr, copy.get())) &&
           SUCCEEDED(copy->Commit(STGC_DEFAULT));
}

/// A new, empty folder under %TEMP%\Osdrop\drags for one drop's virtual files.
std::filesystem::path staging_folder() {
    static std::atomic<unsigned> counter{0};
    std::error_code error;
    const auto folder = std::filesystem::temp_directory_path(error) / L"Osdrop" / L"drags" /
                        (std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++counter));
    std::filesystem::create_directories(folder, error);
    return error ? std::filesystem::path() : folder;
}

/// Saves the virtual files of a drop (FILEGROUPDESCRIPTORW and FILECONTENTS) into staging and
/// returns its top-level items.
std::vector<std::filesystem::path> saved_virtual_files(IDataObject* data) {
    FORMATETC wanted = format(registered(CFSTR_FILEDESCRIPTORW), TYMED_HGLOBAL);
    Medium descriptors;
    if (FAILED(data->GetData(&wanted, descriptors.get()))) {
        return {};
    }
    const auto* group = static_cast<const FILEGROUPDESCRIPTORW*>(GlobalLock(descriptors->hGlobal));
    if (group == nullptr) {
        return {};
    }
    const std::filesystem::path folder = staging_folder();
    std::vector<std::filesystem::path> items;
    const CLIPFORMAT contents = registered(CFSTR_FILECONTENTS);
    const std::span files(&group->fgd[0], group->cItems);
    for (std::size_t i = 0; i < files.size() && !folder.empty(); ++i) {
        const FILEDESCRIPTORW& file = files[i];
        const std::filesystem::path relative = inside(std::data(file.cFileName));
        if (relative.empty()) {
            continue; // a name that would leave the staging folder
        }
        const std::filesystem::path path = folder / relative;
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        const bool is_folder = (file.dwFlags & static_cast<DWORD>(FD_ATTRIBUTES)) != 0 &&
                               (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool saved = false;
        if (is_folder) {
            saved = std::filesystem::create_directories(path, error) || !error;
        } else {
            FORMATETC content = format(contents, TYMED_ISTREAM | TYMED_HGLOBAL | TYMED_ISTORAGE,
                                       static_cast<LONG>(i));
            Medium medium;
            if (SUCCEEDED(data->GetData(&content, medium.get()))) {
                const std::uint64_t size =
                    (file.dwFlags & static_cast<DWORD>(FD_FILESIZE)) != 0
                        ? (std::uint64_t{file.nFileSizeHigh} << 32U) | file.nFileSizeLow
                        : UINT64_MAX;
                switch (medium->tymed) {
                case TYMED_ISTREAM:
                    saved = write_stream(medium->pstm, path);
                    break;
                case TYMED_HGLOBAL:
                    saved = write_global(medium->hGlobal, size, path);
                    break;
                case TYMED_ISTORAGE:
                    saved = write_storage(medium->pstg, path);
                    break;
                default:
                    break;
                }
            }
        }
        if (saved && !relative.has_parent_path()) {
            items.push_back(path);
        }
    }
    GlobalUnlock(descriptors->hGlobal);
    return items;
}

/// Takes drops as copies and hands their files on.
class DropTarget final : public ComObject<IDropTarget> {
public:
    DropTarget(EdgeTargets::Dropped dropped, std::atomic<int>& over)
        : dropped_(std::move(dropped)), over_(over) {}

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD /*keys*/, POINTL /*at*/,
                                        DWORD* effect) override {
        ++over_;
        accepts_ = readable(data);
        return answer(effect);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD /*keys*/, POINTL /*at*/, DWORD* effect) override {
        return answer(effect);
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        --over_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD /*keys*/, POINTL at,
                                   DWORD* effect) override {
        --over_;
        if (!accepts_ || data == nullptr || effect == nullptr || (*effect & DROPEFFECT_COPY) == 0) {
            if (effect != nullptr) {
                *effect = DROPEFFECT_NONE; // the drag slides back
            }
            return S_OK;
        }
        *effect = DROPEFFECT_COPY; // the source keeps its files
        take(data, POINT{.x = at.x, .y = at.y});
        return S_OK;
    }

private:
    static bool readable(IDataObject* data) {
        if (data == nullptr) {
            return false;
        }
        FORMATETC files = format(CF_HDROP, TYMED_HGLOBAL);
        FORMATETC descriptors = format(registered(CFSTR_FILEDESCRIPTORW), TYMED_HGLOBAL);
        return data->QueryGetData(&files) == S_OK || data->QueryGetData(&descriptors) == S_OK;
    }

    HRESULT answer(DWORD* effect) const {
        if (effect == nullptr) {
            return E_INVALIDARG;
        }
        *effect = accepts_ && (*effect & DROPEFFECT_COPY) != 0 ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }

    void take(IDataObject* data, POINT at) {
        std::vector<std::filesystem::path> files = dropped_files(data);
        if (!files.empty()) {
            dropped_(std::move(files), at);
            return;
        }
        // Virtual files: in the background when the source allows it, so it does not wait.
        ComPtr<IDataObjectAsyncCapability> async;
        BOOL asynchronous = FALSE;
        IStream* marshaled = nullptr;
        if (SUCCEEDED(data->QueryInterface(IID_PPV_ARGS(async.put()))) &&
            SUCCEEDED(async->GetAsyncMode(&asynchronous)) && asynchronous != FALSE &&
            SUCCEEDED(async->StartOperation(nullptr)) &&
            SUCCEEDED(CoMarshalInterThreadInterfaceInStream(IID_IDataObject, data, &marshaled))) {
            std::thread([marshaled, at, dropped = dropped_] {
                CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                {
                    ComPtr<IDataObject> remote;
                    if (SUCCEEDED(CoGetInterfaceAndReleaseStream(
                            marshaled, IID_IDataObject, reinterpret_cast<void**>(remote.put())))) {
                        std::vector<std::filesystem::path> saved =
                            saved_virtual_files(remote.get());
                        ComPtr<IDataObjectAsyncCapability> operation;
                        if (SUCCEEDED(remote->QueryInterface(IID_PPV_ARGS(operation.put())))) {
                            operation->EndOperation(saved.empty() ? E_FAIL : S_OK, nullptr,
                                                    DROPEFFECT_COPY);
                        }
                        dropped(std::move(saved), at);
                    }
                }
                CoUninitialize();
            }).detach();
            return;
        }
        if (async && asynchronous != FALSE) {
            async->EndOperation(E_FAIL, nullptr, DROPEFFECT_NONE); // it was started above
        }
        dropped_(saved_virtual_files(data), at);
    }

    EdgeTargets::Dropped dropped_;
    std::atomic<int>& over_;
    bool accepts_ = false;
};

// NOLINTEND(cppcoreguidelines-pro-type-union-access)

} // namespace

EdgeTargets::EdgeTargets(Dropped dropped)
    : dropped_(std::move(dropped)), thread_([this] { run(); }) {}

EdgeTargets::~EdgeTargets() {
    try {
        if (const DWORD id = thread_id_.load(); id != 0) {
            PostThreadMessageW(id, WM_QUIT, 0, 0);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    } catch (...) {
        std::terminate(); // only joining can throw: unrecoverable
    }
}

void EdgeTargets::set_strips(std::vector<win::PixelRect> strips) {
    {
        const std::scoped_lock lock(mutex_);
        wanted_ = std::move(strips);
    }
    if (const DWORD id = thread_id_.load(); id != 0) {
        PostThreadMessageW(id, kPlaceMessage, 0, 0);
    }
}

void EdgeTargets::run() {
    // The strips are placed in physical pixels, as the monitors are measured.
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(OleInitialize(nullptr))) {
        return;
    }
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // creates the message queue
    const WNDCLASSEXW window_class{.cbSize = sizeof(WNDCLASSEXW),
                                   .style = 0,
                                   .lpfnWndProc = DefWindowProcW,
                                   .cbClsExtra = 0,
                                   .cbWndExtra = 0,
                                   .hInstance = GetModuleHandleW(nullptr),
                                   .hIcon = nullptr,
                                   .hCursor = LoadCursorW(nullptr, IDC_ARROW),
                                   .hbrBackground =
                                       static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)),
                                   .lpszMenuName = nullptr,
                                   .lpszClassName = kClassName,
                                   .hIconSm = nullptr};
    RegisterClassExW(&window_class);
    thread_id_ = GetCurrentThreadId();
    place(); // what was set before the queue existed
    const UINT_PTR timer = SetTimer(nullptr, 0, kPollMilliseconds, nullptr);
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.hwnd == nullptr && message.message == kPlaceMessage) {
            place();
        } else if (message.hwnd == nullptr && message.message == WM_TIMER) {
            poll();
        } else {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    KillTimer(nullptr, timer);
    {
        const std::scoped_lock lock(mutex_);
        wanted_.clear();
    }
    place(); // removes the windows
    OleUninitialize();
}

void EdgeTargets::place() {
    for (HWND window : windows_) {
        RevokeDragDrop(window);
        DestroyWindow(window);
    }
    windows_.clear();
    shown_ = false;
    std::vector<win::PixelRect> strips;
    {
        const std::scoped_lock lock(mutex_);
        strips = wanted_;
    }
    if (strips.empty()) {
        return;
    }
    const ComPtr<DropTarget> target(
        new DropTarget(dropped_, over_)); // NOLINT(cppcoreguidelines-owning-memory)
    for (const win::PixelRect& strip : strips) {
        HWND window = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"",
            WS_POPUP, strip.left, strip.top, strip.right - strip.left, strip.bottom - strip.top,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (window == nullptr) {
            continue;
        }
        SetLayeredWindowAttributes(window, 0, kAlpha, LWA_ALPHA);
        RegisterDragDrop(window, target.get());
        windows_.push_back(window);
    }
}

void EdgeTargets::poll() {
    // Shown while the left button is down or a drag is over one of them: an OLE drag holds it.
    const bool held = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool show = held || over_.load() > 0;
    if (show == shown_) {
        return;
    }
    shown_ = show;
    for (HWND window : windows_) {
        if (show) {
            SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else {
            ShowWindow(window, SW_HIDE);
        }
    }
}

} // namespace osdrop::engine::drag
