import Foundation

@testable import Osdrop

/// A pasteboard in memory: tests "copy" by setting its contents and read what was written.
@MainActor
final class FakePasteboard: PasteboardAccess {
    private(set) var changeCount = 0
    private(set) var readCount = 0
    private(set) var written: [PasteboardContent] = []
    private var currentTypes: Set<String> = []
    private var snapshot = PasteboardSnapshot(data: [:], fileURLs: [])

    /// A copy by some app on this Mac.
    func copy(_ data: [String: Data], files: [URL] = [], extraTypes: Set<String> = []) {
        snapshot = PasteboardSnapshot(data: data, fileURLs: files)
        currentTypes = Set(data.keys).union(extraTypes).union(files.isEmpty ? [] : ["public.file-url"])
        changeCount += 1
    }

    func types() -> Set<String> { currentTypes }

    func read() -> PasteboardSnapshot {
        readCount += 1
        return snapshot
    }

    func write(_ content: PasteboardContent) -> Int {
        written.append(content)
        snapshot = PasteboardSnapshot(data: content.data, fileURLs: content.fileURLs)
        currentTypes = Set(content.data.keys)
        changeCount += 1
        return changeCount
    }
}

/// Marks images instead of decoding them.
struct FakeImageConverter: ImageConverting {
    func png(fromTIFF data: Data) -> Data? { Data("png:".utf8) + data }
    func tiff(fromPNG data: Data) -> Data? { Data("tiff:".utf8) + data }
}

extension ClipboardServices {
    @MainActor
    static func fake(
        staging: URL = FileManager.default.temporaryDirectory.appending(path: "osdrop-tests-\(UUID())")
    )
        -> ClipboardServices
    {
        ClipboardServices(
            makePasteboard: { FakePasteboard() }, staging: ClipboardStaging(root: staging),
            converter: FakeImageConverter())
    }
}
