import AppKit

/// PNG travels between computers; Mac apps also copy and paste TIFF.
protocol ImageConverting: Sendable {
    func png(fromTIFF data: Data) -> Data?
    func tiff(fromPNG data: Data) -> Data?
}

/// Converts through a bitmap representation. Safe off the main thread: each call has its own.
struct BitmapImageConverter: ImageConverting {
    func png(fromTIFF data: Data) -> Data? {
        NSBitmapImageRep(data: data)?.representation(using: .png, properties: [:])
    }

    func tiff(fromPNG data: Data) -> Data? {
        NSBitmapImageRep(data: data)?.tiffRepresentation
    }
}
