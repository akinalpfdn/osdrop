import CoreBridge
import CoreGraphics
import Foundation
import Testing

@testable import Osdrop

/// Drag and drop on the Mac with AppKit's parts and the mesh in memory: this Mac's drags reported
/// to core, drags from other computers started and ended, and this Mac's drag ended at the edge.
@MainActor
@Suite("Drag and drop across computers")
struct DragControllerTests {
    private let mesh = FakeControlMesh()
    private let injector = FakeInjector()
    private let watcher = FakeWatcher()
    private let starter = FakeStarter()
    private let edge = FakeEdge()
    private let cursor = FakeCursor()
    private let source = dragDeviceID(0x42)
    private let report = ArrivingItem(name: "Report.pdf", isFolder: false, byteCount: 9, contentType: "com.adobe.pdf")
    private let point = CGPoint(x: 10, y: 20)
    private let stagingRoot = FileManager.default.temporaryDirectory.appendingPathComponent(
        "osdrop-tests-\(UUID().uuidString)")

    private func start(releaseHold: Duration = .milliseconds(10)) -> DragController {
        let controller = DragController(
            mesh: mesh, injector: injector, staging: DragStaging(root: stagingRoot), watcher: watcher,
            starter: starter, edge: edge,
            cursorLocation: { [cursor] in cursor.location },
            timing: DragTiming(
                pressDelay: .milliseconds(1), beginTimeout: .milliseconds(30), releaseSettle: .milliseconds(1),
                releaseHold: releaseHold))
        controller.start()
        return controller
    }

    @Test("A drag on this Mac is reported with its files, and so is its end")
    func reportsLocalDrags() {
        let controller = start()
        #expect(watcher.isRunning)
        let file = DraggedFile(url: URL(filePath: "/Users/me/Report.pdf"), contentType: "com.adobe.pdf")
        watcher.files = [file]
        watcher.onChange?(true)
        watcher.onChange?(false)
        #expect(mesh.calls == [.dragStarted([file]), .dragStopped])
        controller.stop()
        #expect(!watcher.isRunning)
    }

    @Test("A drag that arrives starts from a press at its point and reports how it ended")
    func startsArrivals() async {
        let controller = start()
        await controller.arrived(source: source, dragID: 7, at: point, items: [report])
        #expect(starter.prepared?.items.map(\.name) == ["Report.pdf"])
        #expect(starter.prepared?.items.first?.byteCount == 9)
        #expect(starter.prepared?.point == point)
        #expect(injector.calls == [.button(.left, isDown: true, at: point, clickCount: nil)])
        starter.hasBegun = true
        watcher.onChange?(true)
        #expect(mesh.calls.isEmpty, "Osdrop's own drag is not one of this Mac's")
        starter.finish(.dropped)
        #expect(mesh.calls == [.dropFinished(7, .dropped)])
        controller.stop()
    }

    @Test("A drag that arrives and never begins fails")
    func failsUnstartedArrivals() async throws {
        let controller = start()
        await controller.arrived(source: source, dragID: 7, at: point, items: [report])
        try await waitUntil { mesh.calls == [.dropFinished(7, .failed)] }
        #expect(starter.cancels == 1)
        controller.stop()
    }

    @Test("A drag that moves on ends without a drop: cancelled before it began, Escape after")
    func withdrawsArrivals() async {
        let controller = start()
        await controller.arrived(source: source, dragID: 7, at: point, items: [report])
        controller.withdrawn(7)
        #expect(starter.cancels == 1)
        await controller.arrived(source: source, dragID: 8, at: point, items: [report])
        starter.hasBegun = true
        controller.withdrawn(8)
        let escape = HIDKey.keyboard(DragController.escapeUsage)
        #expect(Array(injector.calls.suffix(2)) == [.key(escape, .down), .key(escape, .up)])
        controller.withdrawn(8)
        #expect(injector.calls.count == 4, "a drag withdrawn once is not withdrawn again")
        controller.stop()
    }

    @Test("This Mac's drag waiting at the edge is released over a drop target at the cursor")
    func endsParkedDrags() async throws {
        let controller = start()
        cursor.location = CGPoint(x: 5, y: 6)
        controller.ended(3, .dropped)
        #expect(edge.calls == [.show(CGPoint(x: 5, y: 6), accepting: true)])
        try await waitUntil { injector.calls == [.releaseDrag(CGPoint(x: 5, y: 6))] }
        try await waitUntil { edge.calls.last == .hide }
        controller.ended(4, .cancelled)
        #expect(edge.calls.last == .show(CGPoint(x: 5, y: 6), accepting: false))
        controller.stop()
    }

    @Test("A drag of promised files is reported as such, and its files are taken at the edge when wanted")
    func takesPromisedFiles() async throws {
        // The files arrive while the drop target still waits for them, however slow the machine.
        let controller = start(releaseHold: .seconds(60))
        watcher.promises = true
        watcher.onChange?(true)
        #expect(mesh.calls == [.dragPromised])
        cursor.location = CGPoint(x: 5, y: 6)
        controller.wanted()
        #expect(edge.calls == [.capture(CGPoint(x: 5, y: 6))])
        try await waitUntil { injector.calls == [.releaseDrag(CGPoint(x: 5, y: 6))] }
        watcher.onChange?(false)
        #expect(mesh.calls == [.dragPromised], "the end of the drag Osdrop dropped is not reported")
        let item = DragItem(
            id: UUID(), name: "IMG_1.heic", isDirectory: false, byteCount: 4, contentType: "public.heic")
        let url = URL(filePath: "/tmp/staged/IMG_1.heic")
        edge.capture(.success(CapturedDrag(items: [item], sources: [item.id: url], stagingFolder: nil)))
        #expect(mesh.calls.last == .dragStarted([DraggedFile(url: url, contentType: "public.heic")]))
        #expect(edge.calls.last == .hide)
        controller.stop()
    }

    @Test("A drag with promises and file URLs is taken as promised: Photos adds a preview's URL")
    func promisesComeBeforeFiles() {
        let controller = start()
        watcher.files = [
            DraggedFile(
                url: URL(filePath: "/Users/me/Pictures/Photos Library.photoslibrary/resources/derivatives/A_1_105_c.jpeg"),
                contentType: "public.jpeg")
        ]
        watcher.promises = true
        watcher.onChange?(true)
        #expect(mesh.calls == [.dragPromised])
        controller.stop()
    }

    @Test("Promised files whose drop does not reach the edge are given up")
    func givesUpUntakenFiles() async throws {
        let controller = start()
        controller.wanted()
        try await waitUntil { mesh.calls == [.dragStarted([])] }
        controller.stop()
    }

    @Test("Stopping ends a drag shown here")
    func stopEndsArrivals() async {
        let controller = start()
        await controller.arrived(source: source, dragID: 7, at: point, items: [report])
        controller.stop()
        #expect(starter.cancels == 1)
        #expect(!watcher.isRunning)
    }

    private func waitUntil(_ condition: @MainActor () -> Bool) async throws {
        for _ in 0..<200 where !condition() {
            try await Task.sleep(for: .milliseconds(10))
        }
        #expect(condition())
    }
}

/// Content of drags from other computers, fetched beside the drop's destination.
@Suite("Content of drags from other computers")
struct NetworkContentReaderTests {
    private struct Fetcher: DragItemFetching {
        let fetch: @Sendable (UInt32, URL) throws -> [URL]

        func fetch(
            _ index: UInt32, into folder: URL, progress: Progress, started: @escaping @Sendable (UInt64) -> Void
        ) throws -> [URL] {
            started(9)
            return try fetch(index, folder)
        }
    }

    private let item = DragItem(
        id: UUID(), name: "Report.pdf", isDirectory: false, byteCount: 3, contentType: "com.adobe.pdf")

    private func makeFolder() throws -> URL {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("osdrop-tests-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder
    }

    @Test("An item arrives beside its destination and is moved there whole")
    func movesIntoPlace() throws {
        let folder = try makeFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let reader = NetworkContentReader(
            fetcher: Fetcher { index, into in
                guard index == 2 else { throw CocoaError(.fileNoSuchFile) }
                let file = into.appendingPathComponent("Report.pdf")
                try Data("abc".utf8).write(to: file)
                return [file]
            }, indices: [item.id: 2])
        let destination = folder.appendingPathComponent(".Report.pdf.partial")
        try reader.write(item, to: destination, progress: Progress(totalUnitCount: 3))
        #expect(try Data(contentsOf: destination) == Data("abc".utf8))
        #expect(try FileManager.default.contentsOfDirectory(atPath: folder.path) == [".Report.pdf.partial"])
    }

    @Test("A failed transfer leaves nothing behind")
    func failureLeavesNothing() throws {
        let folder = try makeFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let reader = NetworkContentReader(
            fetcher: Fetcher { _, _ in throw CoreError.connectionClosed }, indices: [item.id: 0])
        #expect(throws: CoreError.connectionClosed) {
            try reader.write(item, to: folder.appendingPathComponent("x"), progress: Progress())
        }
        #expect(try FileManager.default.contentsOfDirectory(atPath: folder.path).isEmpty)
    }

    @Test("The mesh fetcher waits for the transfer of the drag's item")
    func fetchesThroughTheMesh() throws {
        let file = URL(filePath: "/tmp/Report.pdf")
        let transfers = FakeTransfers(outcome: .success(ReceivedItem(items: [file], skippedLinks: 0)))
        let fetcher = MeshDragFetcher(mesh: transfers, source: dragDeviceID(1), dragID: 5)
        #expect(try fetcher.fetch(1, into: URL(filePath: "/tmp"), progress: Progress()) { _ in } == [file])
        #expect(transfers.received == [TransferItem(kind: .drag, objectID: 5, index: 1)])
    }

    @Test("Cancelling the drop's progress cancels the transfer")
    func cancelsTheTransfer() {
        let transfers = FakeTransfers(outcome: nil)
        let fetcher = MeshDragFetcher(mesh: transfers, source: dragDeviceID(1), dragID: 5)
        let progress = Progress(totalUnitCount: 10)
        DispatchQueue.global().asyncAfter(deadline: .now() + 0.05) { progress.cancel() }
        #expect(throws: CoreError.cancelled) {
            try fetcher.fetch(0, into: URL(filePath: "/tmp"), progress: progress) { _ in }
        }
        #expect(transfers.cancelled == [1])
    }
}

private func dragDeviceID(_ byte: UInt8) -> DeviceID {
    guard let id = DeviceID(bytes: [UInt8](repeating: byte, count: 16)) else {
        preconditionFailure("16 bytes make a device ID")
    }
    return id
}
