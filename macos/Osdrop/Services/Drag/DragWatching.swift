import AppKit
import CoreBridge
import UniformTypeIdentifiers

/// Notices drags on this Mac and reads what they carry.
@MainActor
protocol DragWatching: AnyObject {
    /// Called when a drag starts (true) and when it ends (false).
    var onChange: ((Bool) -> Void)? { get set }
    func start()
    func stop()
    /// The files and folders of the drag in progress.
    func draggedFiles() -> [DraggedFile]
}

/// Watches the drag pasteboard (`DragActivityMonitor`) and reads its file URLs. Promised files
/// (Photos) have no file yet and are not offered.
@MainActor
final class PasteboardDragWatcher: DragWatching {
    private let monitor = DragActivityMonitor()

    var onChange: ((Bool) -> Void)? {
        get { monitor.onChange }
        set { monitor.onChange = newValue }
    }

    func start() {
        monitor.start()
    }

    func stop() {
        monitor.stop()
    }

    func draggedFiles() -> [DraggedFile] {
        let pasteboard = NSPasteboard(name: .drag)
        let urls =
            pasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
        return urls.map { url in
            let values = try? url.resourceValues(forKeys: [.contentTypeKey, .isDirectoryKey])
            let type = values?.contentType ?? (values?.isDirectory == true ? .folder : .data)
            return DraggedFile(url: url, contentType: type.identifier)
        }
    }
}
