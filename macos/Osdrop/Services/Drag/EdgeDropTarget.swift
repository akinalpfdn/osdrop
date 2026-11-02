import AppKit

/// A drag that reached an edge: its items, and the local files that hold their content.
struct CapturedDrag: Sendable, Equatable {
    let items: [DragItem]
    let sources: [UUID: URL]
    /// The staging folder that holds received promises, removed when the transfer is done.
    let stagingFolder: URL?
}

/// What happens to the source app's drag when it is dropped on the edge.
enum EdgeDropDecision: Sendable, Equatable {
    /// The source app sees a completed copy; its files stay where they are.
    case acceptAsCopy
    /// The drag slides back to where it started.
    case reject
}

/// A nearly transparent panel along a screen edge that receives drags while one is in progress
/// (DEVPLAN, macOS constraints). A fully transparent window is no drop target, so it keeps a
/// minimal alpha; it is shown only during drags, so it never takes ordinary clicks.
@MainActor
final class EdgeDropTarget {
    /// Called when a drag enters, with what the pasteboard offers.
    var onEnter: ((_ fileCount: Int, _ promiseCount: Int) -> Void)?
    /// Decides the drop; called synchronously when the source app's drag is released on the edge.
    var decide: (() -> EdgeDropDecision)?
    /// Called once every item is readable from a local file: at once for files, after the source
    /// app has written them for promises.
    var onCapture: ((Result<CapturedDrag, Error>) -> Void)?

    private let staging: DragStaging
    private let panel: NSPanel
    private let view: EdgeDropView

    init(staging: DragStaging, highlighted: Bool = false) {
        self.staging = staging
        panel = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: Theme.Drag.edgeWidth, height: Theme.Drag.edgeWidth),
            styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        view = EdgeDropView(fill: highlighted ? Theme.Drag.highlightedEdgeFill : Theme.Drag.edgeFill)
        panel.contentView = view
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = false
        panel.level = .statusBar
        panel.isReleasedWhenClosed = false
        panel.collectionBehavior = [.canJoinAllSpaces, .transient, .ignoresCycle, .fullScreenAuxiliary]
        view.target = self
    }

    /// Shows the panel over a strip of the screen, in AppKit screen coordinates.
    func show(over frame: NSRect) {
        panel.setFrame(frame, display: true)
        panel.orderFrontRegardless()
    }

    func hide() {
        panel.orderOut(nil)
    }

    fileprivate func entered(_ info: any NSDraggingInfo) -> NSDragOperation {
        let operation = operation(for: info)
        if operation == .copy {
            onEnter?(Self.fileURLs(on: info.draggingPasteboard).count, Self.promises(on: info.draggingPasteboard).count)
        }
        return operation
    }

    /// Copy when the drag carries files or promises and its source allows copying.
    fileprivate func operation(for info: any NSDraggingInfo) -> NSDragOperation {
        let pasteboard = info.draggingPasteboard
        let hasItems = !Self.fileURLs(on: pasteboard).isEmpty || !Self.promises(on: pasteboard).isEmpty
        return hasItems && info.draggingSourceOperationMask.contains(.copy) ? .copy : []
    }

    fileprivate func perform(_ info: any NSDraggingInfo) -> Bool {
        guard decide?() == .acceptAsCopy else { return false }
        let pasteboard = info.draggingPasteboard
        let files = Self.fileURLs(on: pasteboard)
        let promises = Self.promises(on: pasteboard)
        if promises.isEmpty {
            onCapture?(Result { try Self.capture(files: files, promised: [], stagingFolder: nil) })
        } else {
            receive(promises, along: files)
        }
        return true
    }

    /// Promised files are written by the source app after the drop, into a staging folder.
    private func receive(_ promises: [NSFilePromiseReceiver], along files: [URL]) {
        let folder: URL
        do {
            folder = try staging.makeFolder()
        } catch {
            onCapture?(.failure(error))
            return
        }
        let expected = promises.reduce(0) { $0 + max($1.fileTypes.count, 1) }
        let reader = PromiseCollector.reader(expecting: expected) { [weak self] urls, failure in
            Task { @MainActor [weak self] in
                guard let self else { return }
                if let failure {
                    staging.remove(folder)
                    onCapture?(.failure(failure))
                } else {
                    onCapture?(Result { try Self.capture(files: files, promised: urls, stagingFolder: folder) })
                }
            }
        }
        let queue = OperationQueue()
        queue.qualityOfService = .userInitiated
        for promise in promises {
            promise.receivePromisedFiles(atDestination: folder, options: [:], operationQueue: queue, reader: reader)
        }
    }

    private static func capture(files: [URL], promised: [URL], stagingFolder: URL?) throws -> CapturedDrag {
        var items: [DragItem] = []
        var sources: [UUID: URL] = [:]
        for url in files + promised {
            let item = try DragItemDescriber.describe(url)
            items.append(item)
            sources[item.id] = url
        }
        return CapturedDrag(items: items, sources: sources, stagingFolder: stagingFolder)
    }

    private static func fileURLs(on pasteboard: NSPasteboard) -> [URL] {
        pasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
    }

    private static func promises(on pasteboard: NSPasteboard) -> [NSFilePromiseReceiver] {
        pasteboard.readObjects(forClasses: [NSFilePromiseReceiver.self]) as? [NSFilePromiseReceiver] ?? []
    }
}

private final class EdgeDropView: NSView {
    weak var target: EdgeDropTarget?
    private let fill: NSColor

    init(fill: NSColor) {
        self.fill = fill
        super.init(frame: .zero)
        wantsLayer = true
        layer?.backgroundColor = fill.cgColor
        var types: [NSPasteboard.PasteboardType] = [.fileURL]
        types += NSFilePromiseReceiver.readableDraggedTypes.map { NSPasteboard.PasteboardType($0) }
        registerForDraggedTypes(types)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        nil
    }

    override func draggingEntered(_ sender: any NSDraggingInfo) -> NSDragOperation {
        target?.entered(sender) ?? []
    }

    override func draggingUpdated(_ sender: any NSDraggingInfo) -> NSDragOperation {
        target?.operation(for: sender) ?? []
    }

    override func performDragOperation(_ sender: any NSDraggingInfo) -> Bool {
        target?.perform(sender) ?? false
    }
}

/// Collects promised files as the source app delivers them, on whatever queue AppKit uses, and
/// reports once when every one has arrived. Kept free of any actor: a main actor check in the
/// reader ended Osdrop when Photos delivered a photo.
enum PromiseCollector {
    static func reader(
        expecting expected: Int, completion: @escaping @Sendable ([URL], Error?) -> Void
    ) -> @Sendable (URL, Error?) -> Void {
        let received = Locked<(urls: [URL], failure: Error?, count: Int)>(([], nil, 0))
        return { url, error in
            let done = received.withLock { state -> (urls: [URL], failure: Error?)? in
                if let error {
                    state.failure = state.failure ?? error
                } else {
                    state.urls.append(url)
                }
                state.count += 1
                return state.count == expected ? (state.urls, state.failure) : nil
            }
            if let done {
                completion(done.urls, done.failure)
            }
        }
    }
}
