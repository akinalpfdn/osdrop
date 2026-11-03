import Foundation
import Testing

@testable import Osdrop

@Suite("Unique file names")
struct UniqueFileNameTests {
    private func resolve(_ name: String, directory: Bool = false, taken: Set<String>) -> String {
        UniqueFileName.resolve(name, isDirectory: directory) { taken.contains($0) }
    }

    @Test("A free name stays as it is")
    func free() {
        #expect(resolve("Report.pdf", taken: []) == "Report.pdf")
    }

    @Test("Taken names count up before the extension, as Finder does")
    func countsUp() {
        #expect(resolve("Report.pdf", taken: ["Report.pdf"]) == "Report 2.pdf")
        #expect(resolve("Report.pdf", taken: ["Report.pdf", "Report 2.pdf"]) == "Report 3.pdf")
        #expect(resolve("archive.tar.gz", taken: ["archive.tar.gz"]) == "archive.tar 2.gz")
    }

    @Test("Folders, names without an extension and hidden files get the number at the end")
    func numberAtTheEnd() {
        #expect(resolve("Photos.library", directory: true, taken: ["Photos.library"]) == "Photos.library 2")
        #expect(resolve("README", taken: ["README"]) == "README 2")
        #expect(resolve(".env", taken: [".env"]) == ".env 2")
    }
}

/// A temporary folder removed at the end of each test.
private final class TemporaryFolder {
    let url: URL

    init() throws {
        url = FileManager.default.temporaryDirectory.appendingPathComponent("osdrop-tests-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    }

    deinit {
        try? FileManager.default.removeItem(at: url)  // a test that fails may have removed it already
    }

    func file(_ name: String, bytes: Int, in folder: URL? = nil) throws -> URL {
        let file = (folder ?? url).appendingPathComponent(name)
        try Data((0..<bytes).map { UInt8(truncatingIfNeeded: $0) }).write(to: file)
        return file
    }

    func folder(_ name: String, in parent: URL? = nil) throws -> URL {
        let folder = (parent ?? url).appendingPathComponent(name)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder
    }

    func names(in folder: URL) throws -> Set<String> {
        Set(try FileManager.default.contentsOfDirectory(atPath: folder.path))
    }
}

/// Writes part of a file and then fails, like a source that disappears mid-transfer.
private struct FailingReader: PromisedContentReading {
    func write(_ item: DragItem, to destination: URL, progress: Progress) throws {
        try Data(repeating: 1, count: 100).write(to: destination)
        throw CocoaError(.fileReadNoSuchFile)
    }
}

@Suite("Promised item writer")
struct PromisedItemWriterTests {
    private let temporary: TemporaryFolder
    private let source: URL
    private let destination: URL

    init() throws {
        temporary = try TemporaryFolder()
        source = try temporary.folder("source")
        destination = try temporary.folder("destination")
    }

    private func writer(for urls: [URL]) throws -> (PromisedItemWriter, [DragItem]) {
        let items = try urls.map { try DragItemDescriber.describe($0) }
        let sources = Dictionary(uniqueKeysWithValues: zip(items.map(\.id), urls))
        return (PromisedItemWriter(reader: LocalFileContentReader(sources: sources)), items)
    }

    @Test("A file arrives whole, with its dates, and progress reaches its size")
    func file() throws {
        let original = try temporary.file("data.bin", bytes: 9 * 1024 * 1024 + 7, in: source)
        let date = Date(timeIntervalSince1970: 1_700_000_000)
        try FileManager.default.setAttributes([.modificationDate: date], ofItemAtPath: original.path)
        let (writer, items) = try writer(for: [original])
        let progress = Progress()

        let written = try writer.write(items[0], into: destination, progress: progress)

        #expect(written.lastPathComponent == "data.bin")
        #expect(try Data(contentsOf: written) == Data(contentsOf: original))
        #expect(progress.completedUnitCount == Int64(items[0].byteCount))
        let attributes = try FileManager.default.attributesOfItem(atPath: written.path)
        #expect(attributes[.modificationDate] as? Date == date)
        #expect(FileManager.default.fileExists(atPath: original.path))
    }

    @Test("A folder arrives with everything inside it")
    func folderTree() throws {
        let root = try temporary.folder("Project", in: source)
        let nested = try temporary.folder("nested", in: root)
        _ = try temporary.file("a.txt", bytes: 10, in: root)
        _ = try temporary.file("b.txt", bytes: 20, in: nested)
        let (writer, items) = try writer(for: [root])
        #expect(items[0].isDirectory)
        #expect(items[0].byteCount == 30)

        let written = try writer.write(items[0], into: destination, progress: Progress())

        #expect(try temporary.names(in: written) == ["a.txt", "nested"])
        #expect(try temporary.names(in: written.appendingPathComponent("nested")) == ["b.txt"])
    }

    @Test("A name already in the destination gets the next free number")
    func collision() throws {
        let original = try temporary.file("photo.jpg", bytes: 5, in: source)
        _ = try temporary.file("photo.jpg", bytes: 1, in: destination)
        let (writer, items) = try writer(for: [original])

        let written = try writer.write(items[0], into: destination, progress: Progress())

        #expect(written.lastPathComponent == "photo 2.jpg")
        #expect(try temporary.names(in: destination) == ["photo.jpg", "photo 2.jpg"])
    }

    @Test("Cancelling leaves nothing behind")
    func cancelled() throws {
        let original = try temporary.file("big.bin", bytes: 1024, in: source)
        let (writer, items) = try writer(for: [original])
        let progress = Progress()
        progress.cancel()

        #expect(throws: CancellationError.self) {
            try writer.write(items[0], into: destination, progress: progress)
        }
        #expect(try temporary.names(in: destination).isEmpty)
    }

    @Test("A source that fails mid-way leaves nothing behind")
    func failingSource() throws {
        let item = DragItem(
            id: UUID(), name: "lost.bin", isDirectory: false, byteCount: 1000, contentType: "public.data")
        let writer = PromisedItemWriter(reader: FailingReader())

        #expect(throws: CocoaError.self) {
            try writer.write(item, into: destination, progress: Progress())
        }
        #expect(try temporary.names(in: destination).isEmpty)
    }

    @Test("An item whose source is gone fails without writing")
    func missingSource() throws {
        let original = try temporary.file("gone.txt", bytes: 3, in: source)
        let (writer, items) = try writer(for: [original])
        try FileManager.default.removeItem(at: original)

        #expect(throws: (any Error).self) {
            try writer.write(items[0], into: destination, progress: Progress())
        }
        #expect(try temporary.names(in: destination).isEmpty)
    }
}

@Suite("Drag staging")
struct DragStagingTests {
    @Test("Folders are made under the root; only those are removed; removing all clears the root")
    func lifecycle() throws {
        let temporary = try TemporaryFolder()
        let staging = DragStaging(root: temporary.url.appendingPathComponent("drags"))
        let first = try staging.makeFolder()
        let second = try staging.makeFolder()
        #expect(first.path.hasPrefix(staging.root.path))
        #expect(first != second)

        let outside = try temporary.folder("outside")
        staging.remove(outside)
        #expect(FileManager.default.fileExists(atPath: outside.path))

        staging.remove(first)
        #expect(!FileManager.default.fileExists(atPath: first.path))
        staging.removeAll()
        #expect(!FileManager.default.fileExists(atPath: staging.root.path))
    }
}

@Suite("File promise delegate")
struct PromiseFulfillerTests {
    /// Sandboxed drop targets call these through file coordination on a background queue; with a
    /// main actor check in them, Osdrop crashed when a file was dropped on WhatsApp.
    @Test("The promise delegate answers from a background queue")
    func answersOffTheMainActor() async throws {
        let item = DragItem(
            id: UUID(), name: "notes.txt", isDirectory: false, byteCount: 5, contentType: "public.plain-text")
        let fulfiller = PromiseFulfiller(
            writer: PromisedItemWriter(reader: LocalFileContentReader(sources: [:])), staging: DragStaging(),
            report: { _, _ in })
        // Handed to a background queue on purpose, as AppKit does; nothing else touches it meanwhile.
        nonisolated(unsafe) let provider = OsdropFilePromiseProvider(item: item, fulfiller: fulfiller)
        let answers = await withCheckedContinuation { continuation in
            DispatchQueue.global().async {
                let queue =
                    fulfiller.perform(
                        NSSelectorFromString("operationQueueForFilePromiseProvider:"), with: provider)?
                    .takeUnretainedValue() as? OperationQueue
                let name = fulfiller.filePromiseProvider(provider, fileNameForType: "public.plain-text")
                continuation.resume(returning: (queue != nil, name))
            }
        }
        #expect(answers.0)
        #expect(answers.1 == "notes.txt")
    }
}

@Suite("Promise collector")
struct PromiseCollectorTests {
    /// AppKit delivers promised files on a background queue; a main actor check in the reader
    /// ended Osdrop when Photos delivered a photo.
    @Test("Files arriving on background queues are reported once, all together")
    func collectsOffTheMainActor() async {
        let urls = (0..<8).map { URL(fileURLWithPath: "/tmp/promised-\($0)") }
        let reported = await withCheckedContinuation { continuation in
            let calls = Locked(0)
            let reader = PromiseCollector.reader(expecting: urls.count, timeout: .seconds(30)) { received, failure in
                calls.withLock { $0 += 1 }
                continuation.resume(returning: (Set(received), failure == nil, calls.withLock { $0 }))
            }
            for url in urls {
                DispatchQueue.global().async { reader(url, nil) }
            }
        }
        #expect(reported.0 == Set(urls))
        #expect(reported.1)
        #expect(reported.2 == 1)
    }

    @Test("A failed file fails the whole capture")
    func reportsFailure() async {
        let failed = await withCheckedContinuation { continuation in
            let reader = PromiseCollector.reader(expecting: 2, timeout: .seconds(30)) { _, failure in
                continuation.resume(returning: failure != nil)
            }
            DispatchQueue.global().async { reader(URL(fileURLWithPath: "/tmp/a"), nil) }
            DispatchQueue.global().async { reader(URL(fileURLWithPath: "/tmp/b"), CocoaError(.fileReadNoSuchFile)) }
        }
        #expect(failed)
    }
}

@Suite("Promise collector timeout")
struct PromiseCollectorTimeoutTests {
    @Test("A source that never delivers ends the capture after the timeout; late files are ignored")
    func timesOut() async {
        let reports = Locked<[Error?]>([])
        let result = await withCheckedContinuation { continuation in
            let reader = PromiseCollector.reader(expecting: 2, timeout: .milliseconds(100)) { urls, failure in
                reports.withLock { $0.append(failure) }
                continuation.resume(returning: (urls.isEmpty, failure as? PromiseCollector.Failure))
            }
            reader(URL(fileURLWithPath: "/tmp/only-one"), nil)
            DispatchQueue.global().asyncAfter(deadline: .now() + 0.3) {
                reader(URL(fileURLWithPath: "/tmp/too-late"), nil)
            }
        }
        try? await Task.sleep(for: .milliseconds(400))  // lets the late file arrive
        #expect(result.0)
        #expect(result.1 == .timedOut)
        #expect(reports.withLock { $0.count } == 1)
    }
}
