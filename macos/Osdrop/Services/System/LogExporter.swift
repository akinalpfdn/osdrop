import Foundation
import OSLog

enum LogExportError: Error, Equatable, Sendable {
    case unreadable(String)
    case unwritable(String)
}

/// Writes Osdrop's recent log messages to ~/Library/Logs/Osdrop for bug reports (DEVPLAN:
/// Advanced › logs folder). Logging itself stays in the unified log.
protocol LogExporting: Sendable {
    var folder: URL { get }
    func export() async throws(LogExportError) -> URL
    /// The newest `limit` lines of Osdrop's log, oldest first.
    func recentLines(limit: Int) async throws(LogExportError) -> [String]
}

struct UnifiedLogExporter: LogExporting {
    var folder: URL {
        FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask)[0].appending(path: "Logs/Osdrop")
    }

    func export() async throws(LogExportError) -> URL {
        let text = try lines().joined(separator: "\n")
        let formatter = ISO8601DateFormatter()
        let stamp = formatter.string(from: Date()).replacingOccurrences(of: ":", with: "-")
        let file = folder.appending(path: "Osdrop-\(stamp).log")
        do {
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            try (text + "\n").write(to: file, atomically: true, encoding: .utf8)
        } catch {
            throw .unwritable(error.localizedDescription)
        }
        return file
    }

    func recentLines(limit: Int) async throws(LogExportError) -> [String] {
        Array(try lines().suffix(limit))
    }

    private func lines() throws(LogExportError) -> [String] {
        let entries: [OSLogEntryLog]
        do {
            let store = try OSLogStore(scope: .currentProcessIdentifier)
            let since = store.position(date: Date().addingTimeInterval(-Tunables.Logs.exportWindow))
            entries = try store.getEntries(at: since, matching: NSPredicate(format: "subsystem == %@", Log.subsystem))
                .compactMap { $0 as? OSLogEntryLog }
        } catch {
            throw .unreadable(error.localizedDescription)
        }
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return entries.map { entry in
            "\(formatter.string(from: entry.date)) [\(entry.category)] \(entry.composedMessage)"
        }
    }
}
