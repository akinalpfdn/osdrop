import CoreBridge
import Foundation

/// The largest copy of each kind the shared clipboard moves; larger ones stay where they are.
struct ClipboardLimits: Equatable, Sendable {
    var text: UInt64
    var image: UInt64
    var files: UInt64

    static let standard = ClipboardLimits(
        text: Tunables.Clipboard.textLimit, image: Tunables.Clipboard.imageLimit, files: Tunables.Clipboard.filesLimit)

    func limit(for format: ClipboardFormat) -> UInt64 {
        switch format {
        case .text, .rtf, .html: text
        case .image: image
        case .files: files
        }
    }
}

/// What a copy on this Mac shares.
struct LocalCopy: Equatable, Sendable {
    var contents: [ClipboardContent]
    /// An image the pasteboard has only as TIFF: converted to PNG off the main thread.
    var tiff: Data?
    var files: [URL]

    /// The formats of `snapshot` within `limits`; nil when nothing can be shared.
    init?(_ snapshot: PasteboardSnapshot, limits: ClipboardLimits) {
        let candidates: [(String, ClipboardFormat)] = [
            (PasteboardTypes.text, .text), (PasteboardTypes.rtf, .rtf), (PasteboardTypes.html, .html),
            (PasteboardTypes.png, .image),
        ]
        contents = candidates.compactMap { type, format in
            guard let data = snapshot.data[type], UInt64(data.count) <= limits.limit(for: format) else {
                return nil
            }
            return ClipboardContent(format: format, data: data)
        }
        let hasImage = contents.contains { $0.format == .image }
        if !hasImage, let data = snapshot.data[PasteboardTypes.tiff], UInt64(data.count) <= limits.image {
            tiff = data
        } else {
            tiff = nil
        }
        files = snapshot.fileURLs
        if contents.isEmpty && tiff == nil && files.isEmpty {
            return nil
        }
    }
}

/// Another computer's copy as it is written to the pasteboard.
enum RemoteCopy {
    static func content(received: [ClipboardFormat: Data], tiff: Data?, files: [URL]) -> PasteboardContent {
        var data: [String: Data] = [:]
        data[PasteboardTypes.text] = received[.text]
        data[PasteboardTypes.rtf] = received[.rtf]
        data[PasteboardTypes.html] = received[.html]
        data[PasteboardTypes.png] = received[.image]
        data[PasteboardTypes.tiff] = tiff
        return PasteboardContent(data: data, fileURLs: files)
    }
}
