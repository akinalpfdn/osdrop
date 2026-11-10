#include "drag/drag_starter.hpp"

#include "drag/com_object.hpp"

#include <windows.h>

#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace osdrop::engine::drag {
namespace {

namespace shared = win::drag;

constexpr const wchar_t* kClassName = L"OsdropDragStarter";
constexpr int kHalfSize = 8;
constexpr BYTE kAlpha = 1;
/// The press that starts the drag must come this soon after the window shows.
constexpr UINT kPressTimeoutMilliseconds = 3000;
constexpr UINT_PTR kPressTimer = 1;
constexpr int kImageSize = 48;
/// Posted to the drag's thread by DragHandle::cancel().
constexpr UINT kCancelMessage = WM_APP + 2;

CLIPFORMAT registered(const wchar_t* name) {
    return static_cast<CLIPFORMAT>(RegisterClipboardFormatW(name));
}

std::wstring wide(const std::u16string& text) {
    return {text.begin(), text.end()};
}

FILETIME file_time(std::uint64_t value) {
    return {.dwLowDateTime = static_cast<DWORD>(value & 0xFFFFFFFFU),
            .dwHighDateTime = static_cast<DWORD>(value >> 32U)};
}

/// One virtual file's content as an IStream; Explorer reads it on its own thread through COM.
class ContentStream final : public ComObject<IStream> {
public:
    ContentStream(std::shared_ptr<shared::ContentSource> source, std::size_t entry,
                  std::uint64_t size, std::wstring name)
        : source_(std::move(source)), stream_(*source_, entry, size), name_(std::move(name)) {}

    HRESULT STDMETHODCALLTYPE Read(void* buffer, ULONG count, ULONG* read) override {
        if (buffer == nullptr) {
            return STG_E_INVALIDPOINTER;
        }
        const std::size_t got = stream_.read(std::span(static_cast<std::uint8_t*>(buffer), count));
        if (read != nullptr) {
            *read = static_cast<ULONG>(got);
        }
        if (got < count && stream_.broken()) {
            return STG_E_READFAULT; // not the end: the target must not keep a short file
        }
        return got == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(const void* /*buffer*/, ULONG /*count*/,
                                    ULONG* /*written*/) override {
        return STG_E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin,
                                   ULARGE_INTEGER* position) override {
        std::int64_t base = 0;
        switch (origin) {
        case STREAM_SEEK_SET:
            break;
        case STREAM_SEEK_CUR:
            base = static_cast<std::int64_t>(stream_.position());
            break;
        case STREAM_SEEK_END:
            base = static_cast<std::int64_t>(stream_.size());
            break;
        default:
            return STG_E_INVALIDFUNCTION;
        }
        const std::int64_t target = base + move.QuadPart;
        if (target < 0 || !stream_.seek(static_cast<std::uint64_t>(target))) {
            return STG_E_INVALIDFUNCTION;
        }
        if (position != nullptr) {
            position->QuadPart = stream_.position();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER /*size*/) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream* /*to*/, ULARGE_INTEGER /*count*/,
                                     ULARGE_INTEGER* /*read*/,
                                     ULARGE_INTEGER* /*written*/) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD /*flags*/) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER /*offset*/, ULARGE_INTEGER /*count*/,
                                         DWORD /*type*/) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER /*offset*/, ULARGE_INTEGER /*count*/,
                                           DWORD /*type*/) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD flags) override {
        if (stat == nullptr) {
            return STG_E_INVALIDPOINTER;
        }
        *stat = STATSTG{};
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = stream_.size();
        stat->grfMode = STGM_READ;
        if ((flags & STATFLAG_NONAME) == 0) {
            const std::size_t bytes = (name_.size() + 1) * sizeof(wchar_t);
            auto* name = static_cast<wchar_t*>(CoTaskMemAlloc(bytes));
            if (name == nullptr) {
                return STG_E_INSUFFICIENTMEMORY;
            }
            std::memcpy(name, name_.c_str(), bytes);
            stat->pwcsName = name;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** /*clone*/) override { return E_NOTIMPL; }

private:
    std::shared_ptr<shared::ContentSource> source_;
    shared::FileStream stream_;
    std::wstring name_;
};

// STGMEDIUM is OLE's tagged union; tymed says which member is set.
// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)

/// A format set on the object by others (the drag image helper, the drop target).
struct Stored {
    FORMATETC format{};
    STGMEDIUM medium{};
};

/// The drag's files: virtual files, staged CF_HDROP below the cap, and what others set.
class DataObject final : public ComObject<IDataObject, IDataObjectAsyncCapability> {
public:
    DataObject(DragRequest request, DWORD thread)
        : request_(std::move(request)), thread_(thread),
          descriptor_(registered(CFSTR_FILEDESCRIPTORW)), contents_(registered(CFSTR_FILECONTENTS)),
          preferred_(registered(CFSTR_PREFERREDDROPEFFECT)), own_(own_drag_format()) {
        for (const shared::ArrivingItem& item : request_.items) {
            total_ += item.size;
        }
    }
    ~DataObject() override {
        for (Stored& stored : stored_) {
            ReleaseStgMedium(&stored.medium);
        }
    }
    DataObject(const DataObject&) = delete;
    DataObject& operator=(const DataObject&) = delete;
    DataObject(DataObject&&) = delete;
    DataObject& operator=(DataObject&&) = delete;

    [[nodiscard]] bool in_operation() const { return in_operation_; }

    // IDataObject
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
        if (format == nullptr || medium == nullptr) {
            return E_INVALIDARG;
        }
        *medium = STGMEDIUM{};
        if (format->cfFormat == descriptor_ && (format->tymed & TYMED_HGLOBAL) != 0) {
            const shared::VirtualFiles* described = files();
            return described != nullptr ? global(descriptors(*described), medium) : E_FAIL;
        }
        if (format->cfFormat == contents_ && (format->tymed & TYMED_ISTREAM) != 0) {
            return content(format->lindex, medium);
        }
        if (format->cfFormat == CF_HDROP && (format->tymed & TYMED_HGLOBAL) != 0 && stages()) {
            const auto items = request_.content->local_items();
            return items ? global(dropped_files(*items), medium) : E_FAIL;
        }
        if (format->cfFormat == own_ && (format->tymed & TYMED_HGLOBAL) != 0) {
            return global({0}, medium);
        }
        if (format->cfFormat == preferred_ && (format->tymed & TYMED_HGLOBAL) != 0) {
            const DWORD effect = DROPEFFECT_COPY;
            std::vector<std::uint8_t> bytes(sizeof(effect));
            std::memcpy(bytes.data(), &effect, sizeof(effect));
            return global(bytes, medium);
        }
        for (const Stored& stored : stored_) {
            if (stored.format.cfFormat == format->cfFormat &&
                (stored.format.tymed & format->tymed) != 0) {
                return copy(stored, medium);
            }
        }
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* /*format*/, STGMEDIUM* /*medium*/) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
        if (format == nullptr) {
            return E_INVALIDARG;
        }
        const bool ours =
            (format->cfFormat == descriptor_ && (format->tymed & TYMED_HGLOBAL) != 0) ||
            (format->cfFormat == contents_ && (format->tymed & TYMED_ISTREAM) != 0) ||
            (format->cfFormat == CF_HDROP && (format->tymed & TYMED_HGLOBAL) != 0 && stages()) ||
            (format->cfFormat == preferred_ && (format->tymed & TYMED_HGLOBAL) != 0) ||
            (format->cfFormat == own_ && (format->tymed & TYMED_HGLOBAL) != 0);
        const bool theirs = std::ranges::any_of(stored_, [&](const Stored& stored) {
            return stored.format.cfFormat == format->cfFormat &&
                   (stored.format.tymed & format->tymed) != 0;
        });
        return ours || theirs ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* /*in*/, FORMATETC* out) override {
        if (out != nullptr) {
            out->ptd = nullptr;
        }
        return DATA_S_SAMEFORMATETC;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC* format, STGMEDIUM* medium, BOOL release) override {
        if (format == nullptr || medium == nullptr) {
            return E_INVALIDARG;
        }
        Stored stored{.format = *format, .medium = {}};
        stored.format.ptd = nullptr;
        if (release != FALSE) {
            stored.medium = *medium; // ours to release now
        } else if (FAILED(duplicate(*format, *medium, stored.medium))) {
            return E_OUTOFMEMORY;
        }
        const auto same = std::ranges::find_if(stored_, [&](const Stored& existing) {
            return existing.format.cfFormat == format->cfFormat;
        });
        if (same != stored_.end()) {
            ReleaseStgMedium(&same->medium);
            *same = stored;
        } else {
            stored_.push_back(stored);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** formats) override {
        if (formats == nullptr) {
            return E_INVALIDARG;
        }
        if (direction != DATADIR_GET) {
            return E_NOTIMPL;
        }
        std::vector<FORMATETC> list{
            format_of(descriptor_, TYMED_HGLOBAL),
            format_of(contents_, TYMED_ISTREAM, 0),
        };
        if (stages()) {
            list.push_back(format_of(CF_HDROP, TYMED_HGLOBAL));
        }
        list.push_back(format_of(preferred_, TYMED_HGLOBAL));
        list.push_back(format_of(own_, TYMED_HGLOBAL));
        for (const Stored& stored : stored_) {
            list.push_back(stored.format);
        }
        return SHCreateStdEnumFmtEtc(static_cast<UINT>(list.size()), list.data(), formats);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC* /*format*/, DWORD /*flags*/, IAdviseSink* /*sink*/,
                                      DWORD* /*connection*/) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD /*connection*/) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** /*advises*/) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    // IDataObjectAsyncCapability: Explorer reads the files on a thread of its own.
    HRESULT STDMETHODCALLTYPE SetAsyncMode(BOOL asynchronous) override {
        asynchronous_ = asynchronous;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAsyncMode(BOOL* asynchronous) override {
        if (asynchronous == nullptr) {
            return E_INVALIDARG;
        }
        *asynchronous = asynchronous_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StartOperation(IBindCtx* /*context*/) override {
        in_operation_ = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE InOperation(BOOL* in_operation) override {
        if (in_operation == nullptr) {
            return E_INVALIDARG;
        }
        *in_operation = in_operation_ ? TRUE : FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EndOperation(HRESULT /*result*/, IBindCtx* /*context*/,
                                           DWORD /*effect*/) override {
        in_operation_ = false;
        PostThreadMessageW(thread_, WM_NULL, 0, 0); // wakes the thread that waits for this
        return S_OK;
    }

private:
    static FORMATETC format_of(CLIPFORMAT format, DWORD media, LONG index = -1) {
        return {.cfFormat = format,
                .ptd = nullptr,
                .dwAspect = DVASPECT_CONTENT,
                .lindex = index,
                .tymed = media};
    }

    static HRESULT global(const std::vector<std::uint8_t>& bytes, STGMEDIUM* medium) {
        HGLOBAL memory =
            GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, std::max<std::size_t>(bytes.size(), 1));
        if (memory == nullptr) {
            return E_OUTOFMEMORY;
        }
        void* target = GlobalLock(memory);
        if (target != nullptr && !bytes.empty()) {
            std::memcpy(target, bytes.data(), bytes.size());
        }
        GlobalUnlock(memory);
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = memory;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    static HRESULT duplicate(const FORMATETC& format, const STGMEDIUM& from, STGMEDIUM& to) {
        to = from;
        switch (from.tymed) {
        case TYMED_HGLOBAL:
            to.hGlobal = static_cast<HGLOBAL>(OleDuplicateData(from.hGlobal, format.cfFormat, 0));
            return to.hGlobal != nullptr ? S_OK : E_OUTOFMEMORY;
        case TYMED_ISTREAM:
            to.pstm->AddRef();
            break;
        case TYMED_ISTORAGE:
            to.pstg->AddRef();
            break;
        default:
            return DV_E_TYMED;
        }
        if (to.pUnkForRelease != nullptr) {
            to.pUnkForRelease->AddRef();
        }
        return S_OK;
    }

    static HRESULT copy(const Stored& stored, STGMEDIUM* medium) {
        return duplicate(stored.format, stored.medium, *medium);
    }

    /// The files as descriptors, described when a target first asks: the content may wait
    /// until they are known. Null when they cannot be had.
    const shared::VirtualFiles* files() {
        if (!described_) {
            const auto entries = request_.content->entries();
            described_ = true;
            if (entries) {
                files_ = shared::virtual_files(*entries);
            }
        }
        return files_ ? &*files_ : nullptr;
    }

    /// FILEGROUPDESCRIPTORW with one FILEDESCRIPTORW per file and folder.
    [[nodiscard]] static std::vector<std::uint8_t> descriptors(const shared::VirtualFiles& files) {
        const std::size_t header = offsetof(FILEGROUPDESCRIPTORW, fgd);
        std::vector<std::uint8_t> bytes(header + (files.files.size() * sizeof(FILEDESCRIPTORW)));
        const auto count = static_cast<UINT>(files.files.size());
        std::memcpy(bytes.data(), &count, sizeof(count));
        for (std::size_t i = 0; i < files.files.size(); ++i) {
            const shared::VirtualFile& file = files.files[i];
            FILEDESCRIPTORW descriptor{};
            // FD_UNICODE is the sign bit of the enum: the flags are combined as DWORDs.
            descriptor.dwFlags = static_cast<DWORD>(FD_ATTRIBUTES) |
                                 static_cast<DWORD>(FD_FILESIZE) |
                                 static_cast<DWORD>(FD_PROGRESSUI) | static_cast<DWORD>(FD_UNICODE);
            descriptor.dwFileAttributes =
                file.folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            descriptor.nFileSizeHigh = static_cast<DWORD>(file.size >> 32U);
            descriptor.nFileSizeLow = static_cast<DWORD>(file.size & 0xFFFFFFFFU);
            if (file.write_time != 0) {
                descriptor.dwFlags |= static_cast<DWORD>(FD_WRITESTIME);
                descriptor.ftLastWriteTime = file_time(file.write_time);
            }
            const std::wstring path = wide(file.path);
            std::copy_n(path.begin(), std::min(path.size(), shared::kMaxDescriptorPath),
                        std::begin(descriptor.cFileName));
            std::memcpy(bytes.data() + header + (i * sizeof(FILEDESCRIPTORW)), &descriptor,
                        sizeof(descriptor));
        }
        return bytes;
    }

    HRESULT content(LONG index, STGMEDIUM* medium) {
        const shared::VirtualFiles* described = files();
        if (described == nullptr || index < 0 ||
            static_cast<std::size_t>(index) >= described->files.size() ||
            described->files[static_cast<std::size_t>(index)].folder) {
            return DV_E_LINDEX;
        }
        const shared::VirtualFile& file = described->files[static_cast<std::size_t>(index)];
        const std::wstring path = wide(file.path);
        const std::wstring name = path.substr(path.find_last_of(L'\\') + 1);
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): COM object, released by the target
        medium->pstm = new ContentStream(request_.content, file.entry, file.size, name);
        medium->tymed = TYMED_ISTREAM;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    /// Whether the drag is small enough to be offered as local files too.
    [[nodiscard]] bool stages() const {
        return total_ <= request_.staged_limit && !request_.items.empty();
    }

    /// DROPFILES naming the content's top-level items.
    static std::vector<std::uint8_t>
    dropped_files(const std::vector<std::filesystem::path>& items) {
        std::wstring list;
        for (const std::filesystem::path& item : items) {
            list += item.wstring();
            list.push_back(L'\0');
        }
        list.push_back(L'\0');
        DROPFILES header{};
        header.pFiles = sizeof(DROPFILES);
        header.fWide = TRUE;
        std::vector<std::uint8_t> bytes(sizeof(DROPFILES) + (list.size() * sizeof(wchar_t)));
        std::memcpy(bytes.data(), &header, sizeof(header));
        std::memcpy(bytes.data() + sizeof(header), list.data(), list.size() * sizeof(wchar_t));
        return bytes;
    }

    DragRequest request_;
    bool described_ = false;
    std::optional<shared::VirtualFiles> files_; ///< once described; nullopt when that failed
    DWORD thread_;
    CLIPFORMAT descriptor_;
    CLIPFORMAT contents_;
    CLIPFORMAT preferred_;
    CLIPFORMAT own_;
    std::uint64_t total_ = 0;
    std::vector<Stored> stored_;
    BOOL asynchronous_ = TRUE;
    std::atomic<bool> in_operation_{false};
};

// NOLINTEND(cppcoreguidelines-pro-type-union-access)

/// Ends the drag where the left button comes up; Escape or a cancel() ends it without a drop.
class DropSource final : public ComObject<IDropSource> {
public:
    explicit DropSource(const DragHandle& handle) : handle_(handle) {}

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape != FALSE || handle_.cancelled()) {
            return DRAGDROP_S_CANCEL;
        }
        return (keys & MK_LBUTTON) == 0 ? DRAGDROP_S_DROP : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD /*effect*/) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    const DragHandle& handle_;
};

/// The first item's shell icon as the drag image.
void set_drag_image(IDataObject* data, const std::vector<shared::ArrivingItem>& items) {
    if (items.empty()) {
        return;
    }
    ComPtr<IDragSourceHelper> helper;
    if (FAILED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(helper.put())))) {
        return;
    }
    const shared::ArrivingItem& first = items.front();
    SHFILEINFOW info{};
    const std::wstring name = wide(shared::utf16(first.name));
    if (SHGetFileInfoW(name.c_str(),
                       first.folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &info,
                       sizeof(info), SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES) == 0) {
        return;
    }
    BITMAPINFO format{};
    format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    format.bmiHeader.biWidth = kImageSize;
    format.bmiHeader.biHeight = -kImageSize; // top-down
    format.bmiHeader.biPlanes = 1;
    format.bmiHeader.biBitCount = 32;
    format.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(memory, &format, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (bitmap != nullptr) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        DrawIconEx(memory, 0, 0, info.hIcon, kImageSize, kImageSize, 0, nullptr, DI_NORMAL);
        SelectObject(memory, previous);
        SHDRAGIMAGE image{.sizeDragImage = {.cx = kImageSize, .cy = kImageSize},
                          .ptOffset = {.x = kImageSize / 2, .y = kImageSize / 2},
                          .hbmpDragImage = bitmap,
                          .crColorKey = CLR_NONE};
        if (FAILED(helper->InitializeFromBitmap(&image, data))) {
            DeleteObject(bitmap); // the helper owns it only when it took it
        }
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    DestroyIcon(info.hIcon);
}

/// The drag of one starter window, reached from its window procedure.
struct Starter {
    DragRequest request;
    std::function<void(DragOutcome)> ended;
    std::function<void()> done;
    std::shared_ptr<DragHandle> handle;
    DragOutcome outcome = DragOutcome::Failed;
    bool reported = false; ///< `ended` ran
    ComPtr<DataObject> data;

    void end(DragOutcome result) {
        outcome = result;
        if (!reported) {
            reported = true;
            if (ended) {
                ended(result);
            }
        }
    }
};

DragOutcome outcome_of(HRESULT result, DWORD effect) {
    if (result == DRAGDROP_S_DROP && effect != DROPEFFECT_NONE) {
        return DragOutcome::Dropped;
    }
    if (result == DRAGDROP_S_DROP || result == DRAGDROP_S_CANCEL) {
        return DragOutcome::Cancelled;
    }
    return DragOutcome::Failed;
}

LRESULT CALLBACK starter_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr): the pointer stored at creation
    auto* starter = reinterpret_cast<Starter*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        // NOLINTNEXTLINE(performance-no-int-to-ptr): LPARAM of WM_NCCREATE
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        break;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE; // the press is for the drag, the focus stays where it is
    case WM_TIMER:
        KillTimer(window, kPressTimer);
        PostQuitMessage(0); // no press came: the drag never started
        return 0;
    case WM_LBUTTONDOWN:
        if (starter != nullptr && starter->data && !starter->handle->cancelled()) {
            KillTimer(window, kPressTimer);
            ShowWindow(window, SW_HIDE);
            const ComPtr<DropSource> source(
                new DropSource(*starter->handle)); // NOLINT(cppcoreguidelines-owning-memory)
            DWORD effect = DROPEFFECT_NONE;
            const HRESULT result =
                DoDragDrop(starter->data.get(), source.get(), DROPEFFECT_COPY, &effect);
            starter->end(starter->handle->cancelled() ? DragOutcome::Cancelled
                                                      : outcome_of(result, effect));
            PostQuitMessage(0);
            return 0;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void run_drag(Starter& starter) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // creates the message queue
    starter.handle->attach(GetCurrentThreadId());
    if (FAILED(OleInitialize(nullptr))) {
        starter.end(DragOutcome::Failed);
        if (starter.done) {
            starter.done();
        }
        return;
    }
    const WNDCLASSEXW window_class{.cbSize = sizeof(WNDCLASSEXW),
                                   .style = 0,
                                   .lpfnWndProc = starter_procedure,
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
    RegisterClassExW(&window_class); // fails harmlessly when another drag registered it
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): COM object, owned by the ComPtr
    starter.data = ComPtr<DataObject>(new DataObject(starter.request, GetCurrentThreadId()));
    set_drag_image(starter.data.get(), starter.request.items);
    HWND window =
        starter.handle->cancelled()
            ? nullptr
            : CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                              kClassName, L"", WS_POPUP, starter.request.at.x - kHalfSize,
                              starter.request.at.y - kHalfSize, 2 * kHalfSize, 2 * kHalfSize,
                              nullptr, nullptr, GetModuleHandleW(nullptr), &starter);
    if (window != nullptr) {
        SetLayeredWindowAttributes(window, 0, kAlpha, LWA_ALPHA);
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetTimer(window, kPressTimer, kPressTimeoutMilliseconds, nullptr);
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (message.hwnd == nullptr && message.message == kCancelMessage) {
                PostQuitMessage(0); // cancelled before the press: the drag never starts
                continue;
            }
            DispatchMessageW(&message);
        }
        // Explorer may still be reading the files on its own thread, through this one.
        while (starter.data->in_operation() && GetMessageW(&message, nullptr, 0, 0) > 0) {
            DispatchMessageW(&message);
        }
        DestroyWindow(window);
    }
    starter.end(starter.handle->cancelled() ? DragOutcome::Cancelled : starter.outcome);
    starter.data.reset();
    OleUninitialize();
    if (starter.done) {
        starter.done();
    }
}

} // namespace

void DragHandle::cancel() {
    cancelled_ = true;
    if (const DWORD thread = thread_.load(); thread != 0) {
        PostThreadMessageW(thread, kCancelMessage, 0, 0); // wakes it, before or during the drag
    }
}

void DragHandle::attach(DWORD thread) {
    thread_ = thread;
}

CLIPFORMAT own_drag_format() {
    static const CLIPFORMAT format = registered(L"com.akinalpfdn.osdrop.drag");
    return format;
}

std::shared_ptr<DragHandle> start_drag(DragRequest request, std::function<void(DragOutcome)> ended,
                                       std::function<void()> done) {
    auto handle = std::make_shared<DragHandle>();
    try {
        std::thread([starter = Starter{.request = std::move(request),
                                       .ended = std::move(ended),
                                       .done = std::move(done),
                                       .handle = handle,
                                       .outcome = DragOutcome::Failed,
                                       .reported = false,
                                       .data = {}}]() mutable {
            run_drag(starter);
        }).detach();
        return handle;
    } catch (const std::system_error&) {
        return nullptr;
    }
}

} // namespace osdrop::engine::drag
