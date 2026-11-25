import Foundation

/// What the iPhone app and its share extension hand each other through their App Group
/// (DEVPLAN Phase 22): the computers the app last saw, written by the app, and the files the
/// extension was given, each set in a folder of its own with the computer it goes to. The app
/// sends them while it is open; the extension never touches the network.
struct Handoff: Sendable {
    static let appGroup = "group.com.akinalpfdn.osdrop"

    /// A computer the extension can offer.
    struct Computer: Codable, Equatable, Sendable, Identifiable {
        /// The device ID in hex.
        let id: String
        let name: String
        let isWindows: Bool
    }

    /// Files shared to Osdrop and the computer they go to.
    struct PendingSend: Equatable, Sendable {
        /// The set's folder; its files are the folder's other items.
        let folder: URL
        /// The device ID in hex.
        let computer: String
        let files: [URL]
    }

    private struct Target: Codable {
        let computer: String
    }

    private static let computersFile = "Computers.json"
    private static let outboxFolder = "Outbox"
    /// Written last, so a set without it is still being copied (or the copy was cut short).
    private static let targetFile = "target.json"

    let root: URL

    /// The App Group's container; nil when the entitlement is missing.
    static var shared: Handoff? {
        FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: appGroup).map(Handoff.init)
    }

    init(root: URL) {
        self.root = root
    }

    // MARK: - Computers

    func saveComputers(_ computers: [Computer]) throws {
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        try JSONEncoder().encode(computers).write(to: root.appending(path: Self.computersFile), options: .atomic)
    }

    /// The computers the app last saw; none before it ran.
    func computers() -> [Computer] {
        guard let data = try? Data(contentsOf: root.appending(path: Self.computersFile)) else { return [] }
        return (try? JSONDecoder().decode([Computer].self, from: data)) ?? []
    }

    // MARK: - Pending sends

    /// A new, empty folder for one set of shared files.
    func newSet() throws -> URL {
        let folder = root.appending(path: Self.outboxFolder).appending(path: UUID().uuidString)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder
    }

    /// Marks a set complete: from now on the app sends it to `computer`.
    func seal(_ folder: URL, for computer: String) throws {
        try JSONEncoder().encode(Target(computer: computer))
            .write(to: folder.appending(path: Self.targetFile), options: .atomic)
    }

    /// Every complete set, oldest first.
    func pendingSends() -> [PendingSend] {
        let outbox = root.appending(path: Self.outboxFolder)
        let manager = FileManager.default
        let folders =
            (try? manager.contentsOfDirectory(
                at: outbox, includingPropertiesForKeys: [.creationDateKey], options: .skipsHiddenFiles)) ?? []
        return
            folders
            .compactMap { folder -> (Date, PendingSend)? in
                guard let data = try? Data(contentsOf: folder.appending(path: Self.targetFile)),
                    let target = try? JSONDecoder().decode(Target.self, from: data)
                else { return nil }
                let files =
                    ((try? manager.contentsOfDirectory(
                        at: folder, includingPropertiesForKeys: nil, options: .skipsHiddenFiles)) ?? [])
                    .filter { $0.lastPathComponent != Self.targetFile }
                    .sorted { $0.lastPathComponent < $1.lastPathComponent }
                let created = (try? folder.resourceValues(forKeys: [.creationDateKey]))?.creationDate ?? .distantPast
                return (created, PendingSend(folder: folder, computer: target.computer, files: files))
            }
            .sorted { $0.0 < $1.0 }
            .map(\.1)
    }

    /// Removes a set once it was sent, or given up on.
    func remove(_ send: PendingSend) {
        try? FileManager.default.removeItem(at: send.folder)
    }

    /// Removes sets the extension started but never finished.
    func removeUnsealed(olderThan age: TimeInterval, now: Date = .now) {
        let manager = FileManager.default
        let outbox = root.appending(path: Self.outboxFolder)
        let folders =
            (try? manager.contentsOfDirectory(
                at: outbox, includingPropertiesForKeys: [.creationDateKey], options: .skipsHiddenFiles)) ?? []
        for folder in folders where !manager.fileExists(atPath: folder.appending(path: Self.targetFile).path()) {
            let created = (try? folder.resourceValues(forKeys: [.creationDateKey]))?.creationDate ?? .distantPast
            if now.timeIntervalSince(created) > age {
                try? manager.removeItem(at: folder)
            }
        }
    }
}
