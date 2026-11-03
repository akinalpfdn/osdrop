import AppKit
import ApplicationServices
import Testing

@testable import Osdrop

/// The drag bridge end to end with windows of the test's own: an Osdrop promise drag started from
/// injected input into a window that receives promises, and drags captured, or refused, at an edge
/// strip. Runs only through the `OsdropHardware` scheme; nobody may touch the mouse meanwhile.
/// Mouse events are sent only where the test's own window is topmost.
@MainActor
@Suite(
    "Drag bridge on real hardware", .serialized,
    .enabled(if: AXIsProcessTrusted(), "Osdrop needs Accessibility access"))
final class DragHardwareTests {
    private let folder: URL
    private let injector = CGEventInjector()
    private let originalCursor = CGEvent(source: nil)?.location ?? .zero

    init() throws {
        folder = FileManager.default.temporaryDirectory.appendingPathComponent("osdrop-drag-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    }

    deinit {
        try? FileManager.default.removeItem(at: folder)  // nothing to do if a test already removed it
    }

    private func finish() {
        injector.releaseAll()
        injector.moveCursor(to: originalCursor)
    }

    /// Presses at `start`, moves to `end` in steps, hovers and releases, as a hand would.
    private func drag(from start: CGPoint, to end: CGPoint) async throws {
        injector.moveCursor(to: start)
        try await Task.sleep(for: .milliseconds(100))
        injector.button(.left, isDown: true, at: start)
        for step in 1...20 {
            let fraction = Double(step) / 20
            injector.moveCursor(
                to: CGPoint(x: start.x + (end.x - start.x) * fraction, y: start.y + (end.y - start.y) * fraction))
            try await Task.sleep(for: .milliseconds(25))
        }
        for wiggle in [1.0, -1.0, 0.0] {
            injector.moveCursor(to: CGPoint(x: end.x + wiggle, y: end.y))
            try await Task.sleep(for: .milliseconds(120))
        }
        injector.button(.left, isDown: false, at: end)
    }

    private func waitFor(_ condition: () -> Bool, timeout: Duration = .seconds(5)) async throws -> Bool {
        let deadline = ContinuousClock.now + timeout
        while ContinuousClock.now < deadline {
            if condition() {
                return true
            }
            try await Task.sleep(for: .milliseconds(50))
        }
        return condition()
    }

    @Test("An Osdrop drag from injected input delivers a file and a folder as promises")
    func promiseDragDelivers() async throws {
        defer { finish() }
        let source = folder.appendingPathComponent("source")
        let destination = folder.appendingPathComponent("destination")
        try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
        let nested = source.appendingPathComponent("Folder/inner")
        try FileManager.default.createDirectory(at: nested, withIntermediateDirectories: true)
        let file = source.appendingPathComponent("promised.txt")
        try Data("promised by osdrop".utf8).write(to: file)
        try Data("inside".utf8).write(to: nested.appendingPathComponent("deep.txt"))
        let urls = [file, source.appendingPathComponent("Folder")]
        let items = try urls.map { try DragItemDescriber.describe($0) }
        let sources = Dictionary(uniqueKeysWithValues: zip(items.map(\.id), urls))

        let window = PromiseReceivingWindow(destination: destination)
        window.show()
        defer { window.close() }
        let target = window.center
        try #require(await window.waitUntilTopmost(at: target), "The receiving window is not topmost; nothing was sent")

        let starter = PromiseDragStarter(staging: DragStaging(root: folder.appendingPathComponent("staging")))
        let outcome = Locked<DragOutcome?>(nil)
        let origin = CGPoint(x: target.x - 220, y: target.y)
        starter.prepare(items, reader: LocalFileContentReader(sources: sources), at: origin) { result in
            outcome.withLock { $0 = result }
        }
        try await drag(from: origin, to: target)

        #expect(try await waitFor { outcome.withLock { $0 } != nil })
        #expect(outcome.withLock { $0 } == .dropped)
        let delivered = try await waitFor {
            FileManager.default.fileExists(atPath: destination.appendingPathComponent("Folder/inner/deep.txt").path)
                && FileManager.default.fileExists(atPath: destination.appendingPathComponent("promised.txt").path)
        }
        #expect(delivered, "\(window.receivedCount) promises received")
        #expect(try Data(contentsOf: destination.appendingPathComponent("promised.txt")) == Data(contentsOf: file))
        #expect(FileManager.default.fileExists(atPath: file.path))
    }

    @Test(
        "A file dragged onto the edge is captured as a copy; a refused drop is not captured", arguments: [true, false])
    func edgeCapture(accepts: Bool) async throws {
        defer { finish() }
        let file = folder.appendingPathComponent("edge.txt")
        try Data("dragged to the edge".utf8).write(to: file)
        let window = FileDragSourceWindow(file: file)
        window.show()
        defer { window.close() }
        let start = window.center
        try #require(await window.waitUntilTopmost(at: start), "The source window is not topmost; nothing was sent")

        let edge = EdgeDropTarget(staging: DragStaging(root: folder.appendingPathComponent("staging")))
        let captured = Locked<Result<CapturedDrag, Error>?>(nil)
        edge.decide = { accepts ? .acceptAsCopy : .reject }
        edge.onCapture = { result in captured.withLock { $0 = result } }
        let strip = window.strip(rightOf: 160)
        edge.show(over: strip.appKit)
        defer { edge.hide() }

        try await drag(from: start, to: strip.center)

        if accepts {
            #expect(try await waitFor { captured.withLock { $0 } != nil })
            let drag = try #require(try captured.withLock { $0 }?.get())
            #expect(drag.items.map(\.name) == ["edge.txt"])
            #expect(drag.sources.values.first == file)
        } else {
            try await Task.sleep(for: .seconds(1))
            #expect(captured.withLock { $0 } == nil)
        }
        #expect(FileManager.default.fileExists(atPath: file.path))
    }
}

/// Converts between AppKit screen points and the global display points injected events use.
private enum ScreenPoints {
    static var primaryHeight: CGFloat {
        NSScreen.screens.first?.frame.height ?? 0
    }

    static func global(_ point: NSPoint) -> CGPoint {
        CGPoint(x: point.x, y: primaryHeight - point.y)
    }

    static func appKit(_ point: CGPoint) -> NSPoint {
        NSPoint(x: point.x, y: primaryHeight - point.y)
    }
}

/// A floating test window in the middle of the main display.
@MainActor
private class TestDragWindow {
    let window: NSWindow

    init(width: CGFloat, height: CGFloat, title: String, content: NSView) {
        let screen = NSScreen.main?.visibleFrame ?? NSRect(x: 0, y: 0, width: 800, height: 600)
        window = NSWindow(
            contentRect: NSRect(x: screen.midX - width / 2, y: screen.midY - height / 2, width: width, height: height),
            styleMask: [.titled], backing: .buffered, defer: false)
        window.title = title
        window.level = .floating
        window.isReleasedWhenClosed = false
        window.contentView = content
    }

    var center: CGPoint {
        ScreenPoints.global(NSPoint(x: window.frame.midX, y: window.contentLayoutRect.midY + window.frame.minY))
    }

    func show() {
        window.orderFrontRegardless()
    }

    func close() {
        window.orderOut(nil)
    }

    func waitUntilTopmost(at point: CGPoint) async -> Bool {
        for _ in 0..<40 {
            if NSWindow.windowNumber(at: ScreenPoints.appKit(point), belowWindowWithWindowNumber: 0)
                == window.windowNumber
            {
                return true
            }
            try? await Task.sleep(for: .milliseconds(50))  // a cancelled wait just checks once more
        }
        return false
    }

    /// A thin vertical strip beside the window, for an edge target.
    func strip(rightOf distance: CGFloat) -> (appKit: NSRect, center: CGPoint) {
        let frame = window.frame
        let rect = NSRect(x: frame.maxX + distance, y: frame.minY, width: Theme.Drag.edgeWidth, height: frame.height)
        return (rect, ScreenPoints.global(NSPoint(x: rect.midX, y: rect.midY)))
    }
}

/// Receives file promises into a folder, as Finder does.
@MainActor
private final class PromiseReceivingWindow: TestDragWindow {
    private let view: PromiseReceivingView

    var receivedCount: Int {
        view.received.withLock { $0 }
    }

    init(destination: URL) {
        view = PromiseReceivingView(destination: destination)
        super.init(width: 320, height: 220, title: "Osdrop drag test: drop here", content: view)
    }
}

private final class PromiseReceivingView: NSView {
    let destination: URL
    let received = Locked(0)
    private let queue = OperationQueue()

    init(destination: URL) {
        self.destination = destination
        super.init(frame: .zero)
        registerForDraggedTypes(NSFilePromiseReceiver.readableDraggedTypes.map { NSPasteboard.PasteboardType($0) })
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        nil
    }

    override func draggingEntered(_ sender: any NSDraggingInfo) -> NSDragOperation {
        .copy
    }

    override func performDragOperation(_ sender: any NSDraggingInfo) -> Bool {
        let promises =
            sender.draggingPasteboard.readObjects(forClasses: [NSFilePromiseReceiver.self]) as? [NSFilePromiseReceiver]
            ?? []
        let received = received
        for promise in promises {
            promise.receivePromisedFiles(atDestination: destination, options: [:], operationQueue: queue) {
                @Sendable _, _ in
                received.withLock { $0 += 1 }
            }
        }
        return !promises.isEmpty
    }
}

/// Starts a drag of one file URL when pressed, as Finder does.
@MainActor
private final class FileDragSourceWindow: TestDragWindow {
    init(file: URL) {
        super.init(width: 240, height: 160, title: "Osdrop drag test: source", content: FileDragSourceView(file: file))
    }
}

private final class FileDragSourceView: NSView, NSDraggingSource {
    private let file: URL

    init(file: URL) {
        self.file = file
        super.init(frame: .zero)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        nil
    }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool {
        true
    }

    override func mouseDown(with event: NSEvent) {
        let item = NSDraggingItem(pasteboardWriter: file as NSURL)
        let point = convert(event.locationInWindow, from: nil)
        item.setDraggingFrame(NSRect(x: point.x - 16, y: point.y - 16, width: 32, height: 32), contents: nil)
        beginDraggingSession(with: [item], event: event, source: self)
    }

    func draggingSession(
        _ session: NSDraggingSession, sourceOperationMaskFor context: NSDraggingContext
    ) -> NSDragOperation {
        .copy
    }
}
