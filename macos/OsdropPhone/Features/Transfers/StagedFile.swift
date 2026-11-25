import CoreTransferable
import Foundation
import UniformTypeIdentifiers

/// A copy of a picked photo or file in a folder of its own under the temporary folder, so core can
/// read it after the picker let go of the original; the folder goes once the send ended.
struct StagedFile: Transferable, Sendable {
    let url: URL

    var folder: URL { url.deletingLastPathComponent() }

    static var transferRepresentation: some TransferRepresentation {
        FileRepresentation(importedContentType: .item) { received in
            try StagedFile(copying: received.file)
        }
    }

    /// Copies `source` (security scoped or not) into a new staging folder.
    init(copying source: URL) throws {
        let folder = URL.temporaryDirectory.appending(path: "Outgoing").appending(path: UUID().uuidString)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        let scoped = source.startAccessingSecurityScopedResource()
        defer {
            if scoped { source.stopAccessingSecurityScopedResource() }
        }
        let copy = folder.appending(path: source.lastPathComponent)
        try FileManager.default.copyItem(at: source, to: copy)
        url = copy
    }

    /// Staging left by an earlier run.
    static func removeLeftovers() {
        try? FileManager.default.removeItem(at: URL.temporaryDirectory.appending(path: "Outgoing"))
    }
}
