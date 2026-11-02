import Foundation

/// Where the bytes of a promised item come from: a local copy now, the transfer engine over the
/// network in Phase 14. Writers only ever see this interface.
protocol PromisedContentReading: Sendable {
    /// Writes the item (a file, or a folder with everything inside it) at `destination`, which does
    /// not exist yet, counting written bytes on `progress`. Must stop with `CancellationError` when
    /// `progress` is cancelled.
    func write(_ item: DragItem, to destination: URL, progress: Progress) throws
}

/// Copies items from local files, in chunks, so progress moves and cancellation is prompt even for
/// very large files.
struct LocalFileContentReader: PromisedContentReading {
    static let chunkSize = 4 * 1024 * 1024

    let sources: [UUID: URL]

    func write(_ item: DragItem, to destination: URL, progress: Progress) throws {
        guard let source = sources[item.id] else {
            throw CocoaError(.fileNoSuchFile)
        }
        try copy(source, to: destination, progress: progress)
    }

    private func copy(_ source: URL, to destination: URL, progress: Progress) throws {
        let values = try source.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        if values.isSymbolicLink == true {
            let target = try FileManager.default.destinationOfSymbolicLink(atPath: source.path)
            try FileManager.default.createSymbolicLink(atPath: destination.path, withDestinationPath: target)
        } else if values.isDirectory == true {
            try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: false)
            let children = try FileManager.default.contentsOfDirectory(
                at: source, includingPropertiesForKeys: [.isDirectoryKey, .isSymbolicLinkKey])
            for child in children {
                try copy(child, to: destination.appendingPathComponent(child.lastPathComponent), progress: progress)
            }
        } else {
            try copyFile(source, to: destination, progress: progress)
        }
        try copyDates(from: source, to: destination)
    }

    private func copyFile(_ source: URL, to destination: URL, progress: Progress) throws {
        guard FileManager.default.createFile(atPath: destination.path, contents: nil) else {
            throw CocoaError(.fileWriteUnknown)
        }
        let input = try FileHandle(forReadingFrom: source)
        defer { try? input.close() }  // closing a read handle only fails on an already closed handle
        let output = try FileHandle(forWritingTo: destination)
        defer { try? output.close() }  // errors surface from synchronize() below
        while true {
            if progress.isCancelled {
                throw CancellationError()
            }
            guard let chunk = try input.read(upToCount: Self.chunkSize), !chunk.isEmpty else { break }
            try output.write(contentsOf: chunk)
            progress.completedUnitCount += Int64(chunk.count)
        }
        try output.synchronize()
    }

    private func copyDates(from source: URL, to destination: URL) throws {
        let attributes = try FileManager.default.attributesOfItem(atPath: source.path)
        var copied: [FileAttributeKey: Any] = [:]
        copied[.modificationDate] = attributes[.modificationDate]
        copied[.creationDate] = attributes[.creationDate]
        let isLink = (try? destination.resourceValues(forKeys: [.isSymbolicLinkKey]))?.isSymbolicLink == true
        if !isLink {
            try FileManager.default.setAttributes(copied, ofItemAtPath: destination.path)
        }
    }
}
