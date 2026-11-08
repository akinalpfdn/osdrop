import Foundation

/// Folders under the temporary directory where files copied on another computer wait to be
/// pasted. The newest copy's folder stays until the next copy replaces it; leftovers from earlier
/// runs are removed at launch.
struct ClipboardStaging: Sendable {
    let root: URL

    init(
        root: URL = FileManager.default.temporaryDirectory.appendingPathComponent("com.akinalpfdn.osdrop/clipboard")
    ) {
        self.root = root
    }

    /// A new, empty folder for one copy.
    func makeFolder() throws -> URL {
        let folder = root.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder
    }

    /// Removes every folder but `kept`.
    func removeAll(except kept: URL? = nil) {
        let folders =
            (try? FileManager.default.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)) ?? []
        for folder in folders where folder.standardizedFileURL != kept?.standardizedFileURL {
            do {
                try FileManager.default.removeItem(at: folder)
            } catch {
                Log.clipboard.error(
                    "Removing a clipboard staging folder failed: \(error.localizedDescription, privacy: .public)")
            }
        }
    }
}
