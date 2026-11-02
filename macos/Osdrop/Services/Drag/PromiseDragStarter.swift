import AppKit
import UniformTypeIdentifiers

/// How an Osdrop drag ended.
enum DragOutcome: Sendable, Equatable {
    /// The drop target accepted the items; promises are being written.
    case dropped
    /// Escape, a target that accepts nothing, or a drag that never started.
    case cancelled
}

/// Starts drags whose items are file promises (DEVPLAN, macOS constraints). A drag must start from a
/// real mouse-down, so Osdrop places a small window of its own under the point, injects a button
/// press there, and begins the drag session from that event; injected movement then carries it.
@MainActor
final class PromiseDragStarter {
    /// Called on the main actor when each promised item has been written, or failed.
    var onItemWritten: ((DragItem, Result<URL, Error>) -> Void)?

    private let staging: DragStaging
    private let origin: DragOriginPanel
    private var pending: (items: [DragItem], reader: any PromisedContentReading)?
    private var completion: ((DragOutcome) -> Void)?
    private var fulfiller: PromiseFulfiller?

    init(staging: DragStaging) {
        self.staging = staging
        origin = DragOriginPanel()
        origin.originView.onMouseDown = { [weak self] event in
            self?.begin(with: event)
        }
        origin.originView.onEnded = { [weak self] operation in
            self?.finish(operation.contains(.copy) ? .dropped : .cancelled)
        }
    }

    /// Prepares a drag of `items` that starts with the next mouse-down at `point` (global display
    /// coordinates). The caller injects that mouse-down; `completion` reports how the drag ended.
    func prepare(
        _ items: [DragItem], reader: any PromisedContentReading, at point: CGPoint,
        completion: @escaping (DragOutcome) -> Void
    ) {
        finish(.cancelled)
        pending = (items, reader)
        self.completion = completion
        origin.show(centeredAt: Self.appKitPoint(point))
    }

    /// Withdraws a prepared drag that never started.
    func cancel() {
        finish(.cancelled)
    }

    private func begin(with event: NSEvent) {
        guard let pending else { return }
        let fulfiller = PromiseFulfiller(
            writer: PromisedItemWriter(reader: pending.reader), staging: staging,
            report: { [weak self] item, result in
                Task { @MainActor [weak self] in
                    self?.onItemWritten?(item, result)
                }
            })
        self.fulfiller = fulfiller
        let center = origin.originView.bounds.center
        let size = Theme.Drag.iconSize
        let draggingItems = pending.items.enumerated().map { index, item in
            let provider = OsdropFilePromiseProvider(item: item, fulfiller: fulfiller)
            let dragItem = NSDraggingItem(pasteboardWriter: provider)
            let offset = CGFloat(index) * Theme.Drag.stackOffset
            dragItem.setDraggingFrame(
                NSRect(x: center.x - size / 2 + offset, y: center.y - size / 2 - offset, width: size, height: size),
                contents: Self.icon(for: item))
            return dragItem
        }
        let session = origin.originView.beginDraggingSession(
            with: draggingItems, event: event, source: origin.originView)
        session.draggingFormation = .stack
        session.animatesToStartingPositionsOnCancelOrFail = false
        self.pending = nil
    }

    private func finish(_ outcome: DragOutcome) {
        origin.hide()
        pending = nil
        // After a drop, the target may still read its staged copies (Mail keeps attachments by
        // path until the message is sent); they go when Osdrop starts next. A cancelled drag
        // leaves no reader, so its copies go now.
        if outcome == .cancelled {
            fulfiller?.removeStagedCopies()
        }
        let completion = self.completion
        self.completion = nil
        completion?(outcome)
    }

    private static func icon(for item: DragItem) -> NSImage {
        NSWorkspace.shared.icon(for: UTType(item.contentType) ?? (item.isDirectory ? .folder : .data))
    }

    private static func appKitPoint(_ point: CGPoint) -> NSPoint {
        let primaryHeight = NSScreen.screens.first?.frame.height ?? 0
        return NSPoint(x: point.x, y: primaryHeight - point.y)
    }
}

/// Writes promised items on a background queue when the drop target asks for them, and hands
/// staged copies to targets that accept only file URLs.
final class PromiseFulfiller: NSObject, NSFilePromiseProviderDelegate, @unchecked Sendable {
    private let writer: PromisedItemWriter
    private let staging: DragStaging
    private let report: @Sendable (DragItem, Result<URL, Error>) -> Void
    private let stagedFolders = Locked<[URL]>([])
    private let queue: OperationQueue = {
        let queue = OperationQueue()
        queue.qualityOfService = .userInitiated
        queue.name = "Osdrop file promises"
        return queue
    }()

    init(
        writer: PromisedItemWriter, staging: DragStaging,
        report: @escaping @Sendable (DragItem, Result<URL, Error>) -> Void
    ) {
        self.writer = writer
        self.staging = staging
        self.report = report
    }

    func filePromiseProvider(_ filePromiseProvider: NSFilePromiseProvider, fileNameForType fileType: String) -> String {
        (filePromiseProvider as? OsdropFilePromiseProvider)?.item.name ?? fileType
    }

    func operationQueue(for filePromiseProvider: NSFilePromiseProvider) -> OperationQueue {
        queue
    }

    func filePromiseProvider(
        _ filePromiseProvider: NSFilePromiseProvider, writePromiseTo url: URL,
        completionHandler: @escaping ((any Error)?) -> Void
    ) {
        guard let item = (filePromiseProvider as? OsdropFilePromiseProvider)?.item else {
            completionHandler(CocoaError(.fileWriteUnknown))
            return
        }
        let progress = Progress(totalUnitCount: Int64(clamping: item.byteCount))
        do {
            let written = try writer.write(item, into: url.deletingLastPathComponent(), progress: progress)
            report(item, .success(written))
            completionHandler(nil)
        } catch {
            Log.drag.error("Writing a promised item failed: \(error.localizedDescription, privacy: .public)")
            report(item, .failure(error))
            completionHandler(error)
        }
    }

    func removeStagedCopies() {
        let folders = stagedFolders.withLock { folders in
            defer { folders.removeAll() }
            return folders
        }
        folders.forEach(staging.remove)
    }

    /// A complete copy in a staging folder, for drop targets that read only file URLs. Written
    /// synchronously, so it is offered only below `Tunables.Drag.stagedURLLimit`.
    func stagedCopy(of item: DragItem) -> URL? {
        guard item.byteCount <= Tunables.Drag.stagedURLLimit else { return nil }
        do {
            let folder = try staging.makeFolder()
            stagedFolders.withLock { $0.append(folder) }
            return try writer.write(item, into: folder, progress: Progress(totalUnitCount: Int64(item.byteCount)))
        } catch {
            Log.drag.error("Staging a copy failed: \(error.localizedDescription, privacy: .public)")
            return nil
        }
    }
}

/// A file promise that also offers a file URL to a staged copy, for apps that do not take promises
/// (DEVPLAN, macOS constraints).
final class OsdropFilePromiseProvider: NSFilePromiseProvider {
    let item: DragItem
    private let fulfiller: PromiseFulfiller
    private var stagedURL: URL?

    init(item: DragItem, fulfiller: PromiseFulfiller) {
        self.item = item
        self.fulfiller = fulfiller
        super.init()
        fileType = item.contentType
        delegate = fulfiller
    }

    override func writableTypes(for pasteboard: NSPasteboard) -> [NSPasteboard.PasteboardType] {
        let types = super.writableTypes(for: pasteboard)
        return item.byteCount <= Tunables.Drag.stagedURLLimit ? types + [.fileURL] : types
    }

    override func pasteboardPropertyList(forType type: NSPasteboard.PasteboardType) -> Any? {
        guard type == .fileURL else {
            return super.pasteboardPropertyList(forType: type)
        }
        if stagedURL == nil {
            stagedURL = fulfiller.stagedCopy(of: item)
        }
        return (stagedURL as NSURL?)?.pasteboardPropertyList(forType: .fileURL)
    }
}

/// A small window that receives the injected mouse-down a drag starts from.
@MainActor
private final class DragOriginPanel {
    let originView = DragOriginView()
    private let panel: NSPanel

    init() {
        let size = Theme.Drag.originSize
        panel = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: size, height: size),
            styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        panel.contentView = originView
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = false
        panel.level = .screenSaver
        panel.isReleasedWhenClosed = false
        panel.collectionBehavior = [.canJoinAllSpaces, .transient, .ignoresCycle, .fullScreenAuxiliary]
    }

    func show(centeredAt point: NSPoint) {
        let size = Theme.Drag.originSize
        panel.setFrame(NSRect(x: point.x - size / 2, y: point.y - size / 2, width: size, height: size), display: true)
        panel.orderFrontRegardless()
    }

    func hide() {
        panel.orderOut(nil)
    }
}

private final class DragOriginView: NSView, NSDraggingSource {
    var onMouseDown: ((NSEvent) -> Void)?
    var onEnded: ((NSDragOperation) -> Void)?

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
        layer?.backgroundColor = Theme.Drag.edgeFill.cgColor
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        nil
    }

    // The injected press arrives while Osdrop is in the background.
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool {
        true
    }

    override func mouseDown(with event: NSEvent) {
        onMouseDown?(event)
    }

    func draggingSession(
        _ session: NSDraggingSession, sourceOperationMaskFor context: NSDraggingContext
    ) -> NSDragOperation {
        .copy
    }

    func draggingSession(_ session: NSDraggingSession, endedAt screenPoint: NSPoint, operation: NSDragOperation) {
        onEnded?(operation)
    }
}

extension NSRect {
    fileprivate var center: NSPoint {
        NSPoint(x: midX, y: midY)
    }
}
