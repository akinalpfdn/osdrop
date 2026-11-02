import Foundation

/// Folders under the temporary directory where drags keep files that exist only for a moment:
/// promised files received from the source app, and copies handed to apps that take only file
/// URLs. Leftovers from earlier runs are removed at launch.
struct DragStaging: Sendable {
    let root: URL

    init(root: URL = FileManager.default.temporaryDirectory.appendingPathComponent("com.akinalpfdn.osdrop/drags")) {
        self.root = root
    }

    /// A new, empty folder for one drag.
    func makeFolder() throws -> URL {
        let folder = root.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder
    }

    func remove(_ folder: URL) {
        guard folder.path.hasPrefix(root.path) else { return }
        do {
            try FileManager.default.removeItem(at: folder)
        } catch {
            Log.drag.error("Removing a drag staging folder failed: \(error.localizedDescription, privacy: .public)")
        }
    }

    /// Removes every staging folder; called at launch, when no drag can be running.
    func removeAll() {
        guard FileManager.default.fileExists(atPath: root.path) else { return }
        do {
            try FileManager.default.removeItem(at: root)
        } catch {
            Log.drag.error("Removing old drag staging failed: \(error.localizedDescription, privacy: .public)")
        }
    }
}
