import Foundation

/// Fulfils a file promise: writes one item into the folder the drop target named, under a free
/// name, never leaving a partial file behind. The item is written under a hidden temporary name
/// and moved into place only when complete.
struct PromisedItemWriter: Sendable {
    let reader: any PromisedContentReading

    /// Writes `item` into `directory`, publishing progress there for Finder, and returns where it
    /// ended up.
    func write(_ item: DragItem, into directory: URL, progress: Progress) throws -> URL {
        let name = UniqueFileName.resolve(item.name, isDirectory: item.isDirectory, in: directory)
        let destination = directory.appendingPathComponent(name)
        let partial = directory.appendingPathComponent(".\(name).\(UUID().uuidString).osdrop-partial")
        progress.totalUnitCount = Int64(clamping: item.byteCount)
        progress.kind = .file
        progress.fileOperationKind = .copying
        progress.fileURL = destination
        progress.publish()
        defer { progress.unpublish() }
        do {
            try reader.write(item, to: partial, progress: progress)
            if progress.isCancelled {
                throw CancellationError()
            }
            // Another drop may have taken the name while this one was writing.
            let finalName = UniqueFileName.resolve(name, isDirectory: item.isDirectory, in: directory)
            let final = directory.appendingPathComponent(finalName)
            try FileManager.default.moveItem(at: partial, to: final)
            return final
        } catch {
            try? FileManager.default.removeItem(at: partial)  // nothing else to do if it is already gone
            throw error
        }
    }
}
