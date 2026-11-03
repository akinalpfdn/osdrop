import Foundation
import UniformTypeIdentifiers

/// A top-level file or folder in a drag, described the way the receiving side needs it before any
/// content moves (DEVPLAN, drag semantics).
struct DragItem: Sendable, Equatable, Identifiable {
    let id: UUID
    let name: String
    let isDirectory: Bool
    /// Bytes of the file, or of every file inside the folder.
    let byteCount: UInt64
    /// Uniform type identifier, for the promise and the drag image.
    let contentType: String
}

/// Describes local files and folders as drag items.
enum DragItemDescriber {
    enum Failure: Error, Equatable {
        case unreadable(String)
    }

    static func describe(_ url: URL, id: UUID = UUID()) throws -> DragItem {
        let keys: Set<URLResourceKey> = [.isDirectoryKey, .fileSizeKey, .contentTypeKey, .isSymbolicLinkKey]
        guard let values = try? url.resourceValues(forKeys: keys) else {
            throw Failure.unreadable(url.lastPathComponent)
        }
        let isDirectory = values.isDirectory ?? false
        let type = values.contentType ?? (isDirectory ? .folder : .data)
        return DragItem(
            id: id, name: url.lastPathComponent, isDirectory: isDirectory,
            byteCount: isDirectory ? try folderByteCount(url) : UInt64(values.fileSize ?? 0),
            contentType: type.identifier)
    }

    /// Sum of the regular files inside a folder; symbolic links are not followed.
    static func folderByteCount(_ url: URL) throws -> UInt64 {
        let keys: [URLResourceKey] = [.isRegularFileKey, .fileSizeKey]
        guard let enumerator = FileManager.default.enumerator(at: url, includingPropertiesForKeys: keys) else {
            throw Failure.unreadable(url.lastPathComponent)
        }
        var total: UInt64 = 0
        for case let child as URL in enumerator {
            let values = try child.resourceValues(forKeys: Set(keys))
            if values.isRegularFile == true {
                total += UInt64(values.fileSize ?? 0)
            }
        }
        return total
    }
}
