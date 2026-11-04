#include "clipboard/wic_images.hpp"

#include <windows.h>

#include <objidl.h>
#include <wincodec.h>

#include <cstddef>
#include <cstring>
#include <utility>

namespace osdrop::engine {
namespace {

constexpr std::uint16_t kBitmapMagic = 0x4D42; // "BM"
constexpr std::size_t kBitfieldMasks = 3 * sizeof(DWORD);
constexpr std::size_t kAlphaBitfieldMasks = 4 * sizeof(DWORD);
constexpr std::uint64_t kMaxPixelBytes = std::uint64_t{1} << 30U;
constexpr DWORD kAlphaBitfields = 6; // BI_ALPHABITFIELDS, missing from older SDK headers

/// Owns one COM interface pointer.
template <typename Interface>
class Com {
public:
    Com() = default;
    ~Com() { reset(); }
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    Com(Com&& other) noexcept : pointer_(std::exchange(other.pointer_, nullptr)) {}
    Com& operator=(Com&&) = delete;

    [[nodiscard]] Interface* get() const { return pointer_; }
    Interface* operator->() const { return pointer_; }
    /// For functions that hand out a new reference.
    Interface** put() {
        reset();
        return &pointer_;
    }
    explicit operator bool() const { return pointer_ != nullptr; }

private:
    void reset() {
        if (pointer_ != nullptr) {
            pointer_->Release();
            pointer_ = nullptr;
        }
    }
    Interface* pointer_ = nullptr;
};

Com<IWICImagingFactory> make_factory() {
    Com<IWICImagingFactory> factory;
    IWICImagingFactory* created = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&created)))) {
        *factory.put() = created;
    }
    return factory;
}

/// An encoded image in memory and the first frame decoded from it. `bytes` stays alive with the
/// decoder, which reads from it.
struct Decoded {
    std::vector<std::uint8_t> bytes;
    Com<IWICStream> stream;
    Com<IWICBitmapDecoder> decoder;
    Com<IWICBitmapFrameDecode> frame;
};

bool decode(IWICImagingFactory* factory, Decoded& image) {
    return SUCCEEDED(factory->CreateStream(image.stream.put())) &&
           SUCCEEDED(image.stream->InitializeFromMemory(image.bytes.data(),
                                                        static_cast<DWORD>(image.bytes.size()))) &&
           SUCCEEDED(factory->CreateDecoderFromStream(
               image.stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, image.decoder.put())) &&
           SUCCEEDED(image.decoder->GetFrame(0, image.frame.put()));
}

Com<IWICFormatConverter> to_bgra(IWICImagingFactory* factory, IWICBitmapSource* source) {
    Com<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                     nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        return {};
    }
    return converter;
}

/// The bytes written to a stream on global memory.
std::optional<std::vector<std::uint8_t>> bytes_of(IStream* stream) {
    STATSTG stat{};
    HGLOBAL global = nullptr;
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) ||
        FAILED(GetHGlobalFromStream(stream, &global))) {
        return std::nullopt;
    }
    const auto size = static_cast<std::size_t>(stat.cbSize.QuadPart);
    const void* data = GlobalLock(global);
    if (data == nullptr) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(size);
    std::memcpy(bytes.data(), data, size);
    GlobalUnlock(global);
    return bytes;
}

std::optional<std::vector<std::uint8_t>> encode_png(IWICImagingFactory* factory,
                                                    IWICBitmapSource* source) {
    Com<IStream> stream;
    Com<IWICBitmapEncoder> encoder;
    Com<IWICBitmapFrameEncode> frame;
    UINT width = 0;
    UINT height = 0;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.put())) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())) ||
        FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.put(), nullptr)) ||
        FAILED(frame->Initialize(nullptr)) || FAILED(source->GetSize(&width, &height)) ||
        FAILED(frame->SetSize(width, height)) || FAILED(frame->SetPixelFormat(&format)) ||
        FAILED(frame->WriteSource(source, nullptr)) || FAILED(frame->Commit()) ||
        FAILED(encoder->Commit())) {
        return std::nullopt;
    }
    return bytes_of(stream.get());
}

/// Room for a header that the copy reads from the start of `bytes`.
template <typename Header>
std::optional<Header> header_of(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < sizeof(Header)) {
        return std::nullopt;
    }
    Header header{};
    std::memcpy(&header, bytes.data(), sizeof(Header));
    return header;
}

} // namespace

std::optional<std::vector<std::uint8_t>> png_of_dib(std::span<const std::uint8_t> dib) {
    const auto info = header_of<BITMAPINFOHEADER>(dib);
    if (!info || info->biSize < sizeof(BITMAPINFOHEADER) || info->biSize > dib.size()) {
        return std::nullopt;
    }
    // A BMP file is the DIB behind a file header that says where its pixels start.
    std::size_t masks = 0;
    if (info->biSize == sizeof(BITMAPINFOHEADER) && info->biCompression == BI_BITFIELDS) {
        masks = kBitfieldMasks;
    } else if (info->biSize == sizeof(BITMAPINFOHEADER) && info->biCompression == kAlphaBitfields) {
        masks = kAlphaBitfieldMasks;
    }
    std::size_t colors = info->biClrUsed;
    if (colors == 0 && info->biBitCount <= 8) {
        colors = std::size_t{1} << info->biBitCount;
    }
    const std::size_t offset =
        sizeof(BITMAPFILEHEADER) + info->biSize + masks + (colors * sizeof(RGBQUAD));
    const BITMAPFILEHEADER file{.bfType = kBitmapMagic,
                                .bfSize = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + dib.size()),
                                .bfReserved1 = 0,
                                .bfReserved2 = 0,
                                .bfOffBits = static_cast<DWORD>(offset)};
    Decoded image;
    image.bytes.resize(sizeof(BITMAPFILEHEADER) + dib.size());
    std::memcpy(image.bytes.data(), &file, sizeof(BITMAPFILEHEADER));
    std::memcpy(image.bytes.data() + sizeof(BITMAPFILEHEADER), dib.data(), dib.size());

    const Com<IWICImagingFactory> factory = make_factory();
    if (!factory || !decode(factory.get(), image)) {
        return std::nullopt;
    }
    const Com<IWICFormatConverter> bgra = to_bgra(factory.get(), image.frame.get());
    if (!bgra) {
        return std::nullopt;
    }
    return encode_png(factory.get(), bgra.get());
}

std::optional<std::vector<std::uint8_t>> dib_of_png(std::span<const std::uint8_t> png) {
    const Com<IWICImagingFactory> factory = make_factory();
    Decoded image;
    image.bytes.assign(png.begin(), png.end());
    if (!factory || !decode(factory.get(), image)) {
        return std::nullopt;
    }
    const Com<IWICFormatConverter> bgra = to_bgra(factory.get(), image.frame.get());
    UINT width = 0;
    UINT height = 0;
    if (!bgra || FAILED(bgra->GetSize(&width, &height)) || width == 0 || height == 0) {
        return std::nullopt;
    }
    const std::uint64_t stride = std::uint64_t{width} * 4;
    const std::uint64_t size = stride * height;
    if (size > kMaxPixelBytes) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size));
    if (FAILED(bgra->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(size),
                                pixels.data()))) {
        return std::nullopt;
    }
    const BITMAPINFOHEADER info{.biSize = sizeof(BITMAPINFOHEADER),
                                .biWidth = static_cast<LONG>(width),
                                .biHeight = static_cast<LONG>(height), // bottom-up rows
                                .biPlanes = 1,
                                .biBitCount = 32,
                                .biCompression = BI_RGB,
                                .biSizeImage = static_cast<DWORD>(size),
                                .biXPelsPerMeter = 0,
                                .biYPelsPerMeter = 0,
                                .biClrUsed = 0,
                                .biClrImportant = 0};
    std::vector<std::uint8_t> dib(sizeof(BITMAPINFOHEADER) + pixels.size());
    std::memcpy(dib.data(), &info, sizeof(BITMAPINFOHEADER));
    const auto row = static_cast<std::size_t>(stride);
    for (std::size_t y = 0; y < height; ++y) {
        std::memcpy(dib.data() + sizeof(BITMAPINFOHEADER) + (y * row),
                    pixels.data() + ((height - 1 - y) * row), row);
    }
    return dib;
}

} // namespace osdrop::engine
