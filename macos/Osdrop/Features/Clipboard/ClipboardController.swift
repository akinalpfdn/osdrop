import CoreBridge
import Foundation
import Observation

/// Why the last copy from another computer is not on the pasteboard.
enum ClipboardNotice: Equatable {
    /// Nothing of it could be fetched: the computer left, or refused.
    case unavailable(name: String)
    /// It is larger than this Mac takes through the clipboard; drag and drop moves it.
    case tooLarge(name: String)
}

/// The shared clipboard on this Mac (DEVPLAN, Phase 15). A copy here is read from the pasteboard
/// and reported to core, which announces it to the members; concealed copies (passwords) never
/// leave. A copy on another computer is fetched in the background, within limits, and written to
/// the pasteboard as real data once everything has arrived, so a paste never waits on the network
/// (DECISIONS.md: macOS asks for lazily provided data at once).
@MainActor
@Observable
final class ClipboardController {
    private(set) var notice: ClipboardNotice?

    private let mesh: any MeshDriving
    private let pasteboard: any PasteboardAccess
    private let staging: ClipboardStaging
    private let converter: any ImageConverting
    private let currentLimits: @MainActor () -> ClipboardLimits
    private let marksFiles: @MainActor () -> Bool
    /// True while a member is connected: copies are read and reported only then.
    private let isShared: @MainActor () -> Bool
    private let nameOf: @MainActor (DeviceID) -> String

    private var seenChange: Int
    private var ownChange: Int?
    private var polling: Task<Void, Never>?
    /// Copies are reported in the order they were made.
    private var reporting: Task<Void, Never>?
    private var incoming: Incoming?
    /// Results that arrived before their request returned its ID.
    private var early: [UInt64: Result<Fetched, CoreError>] = [:]

    private enum Fetched {
        case content(Data)
        case files([URL])
    }

    /// Another computer's copy while it is being fetched.
    private struct Incoming {
        let clipID: UInt64
        let device: DeviceID
        var requests = 0
        var settled = 0
        var pending: [UInt64: ClipboardFormat] = [:]
        var received: [ClipboardFormat: Data] = [:]
        var files: [URL] = []
        var folder: URL?
        var tooLarge = false

        var isComplete: Bool { settled == requests && pending.isEmpty }
    }

    init(
        mesh: any MeshDriving, pasteboard: any PasteboardAccess, staging: ClipboardStaging,
        converter: any ImageConverting, limits: @escaping @MainActor () -> ClipboardLimits = { .standard },
        marksFiles: @escaping @MainActor () -> Bool = { false },
        isShared: @escaping @MainActor () -> Bool, nameOf: @escaping @MainActor (DeviceID) -> String
    ) {
        self.mesh = mesh
        self.pasteboard = pasteboard
        self.staging = staging
        self.converter = converter
        self.currentLimits = limits
        self.marksFiles = marksFiles
        self.isShared = isShared
        self.nameOf = nameOf
        seenChange = pasteboard.changeCount
    }

    func start() {
        guard polling == nil else { return }
        staging.removeAll()
        seenChange = pasteboard.changeCount
        polling = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(for: Tunables.Clipboard.pollInterval)
                self?.poll()
            }
        }
    }

    func stop() {
        polling?.cancel()
        polling = nil
    }

    func dismissNotice() {
        notice = nil
    }

    // MARK: - Copies on this Mac

    /// Looks for a new copy on the pasteboard; the polling task calls it.
    func poll() {
        let change = pasteboard.changeCount
        guard change != seenChange else { return }
        seenChange = change
        guard change != ownChange, isShared() else { return }
        guard pasteboard.types().isDisjoint(with: PasteboardTypes.sensitive) else {
            Log.clipboard.info("A concealed copy stays on this Mac")
            return
        }
        let limits = currentLimits()
        guard let copy = LocalCopy(pasteboard.read(), limits: limits) else { return }
        incoming = nil  // this copy is newer than the one being fetched
        let previous = reporting
        reporting = Task { [mesh, converter, limits] in
            await previous?.value
            var contents = copy.contents
            if let tiff = copy.tiff {
                let png = await Task.detached { converter.png(fromTIFF: tiff) }.value
                if let png, UInt64(png.count) <= limits.image {
                    contents.append(ClipboardContent(format: .image, data: png))
                }
            }
            guard !contents.isEmpty || !copy.files.isEmpty else { return }
            do throws(CoreError) {
                _ = try await mesh.clipboardCopied(contents, files: copy.files)
            } catch {
                Log.clipboard.error("Reporting a copy failed: \(String(describing: error), privacy: .public)")
            }
        }
    }

    // MARK: - Copies on other computers

    func handle(_ event: MeshEvent) {
        switch event {
        case .clipboardChanged(let device, let clipID, let formats):
            guard isShared() else { return }  // sharing turned off in Settings
            begin(device: device, clipID: clipID, formats: formats)
        case .contentReceived(let id, let result):
            settle(id, result.map { .content($0) })
        case .transferFinished(let id, let result):
            settle(id, result.map { .files($0.items) })
        default:
            break
        }
    }

    private func begin(device: DeviceID, clipID: UInt64, formats: [AnnouncedFormat]) {
        var copy = Incoming(clipID: clipID, device: device)
        early.removeAll()
        var wanted: [AnnouncedFormat] = []
        for format in formats {
            if format.byteCount > currentLimits().limit(for: format.format) {
                copy.tooLarge = true
            } else {
                wanted.append(format)
            }
        }
        if wanted.contains(where: { $0.format == .files }) {
            do {
                copy.folder = try staging.makeFolder()
            } catch {
                Log.clipboard.error("No staging folder: \(error.localizedDescription, privacy: .public)")
                wanted.removeAll { $0.format == .files }
            }
        }
        copy.requests = wanted.count
        incoming = copy
        guard !wanted.isEmpty else {
            if copy.tooLarge {
                notice = .tooLarge(name: nameOf(device))
            }
            return
        }
        for format in wanted {
            request(format.format, of: copy)
        }
    }

    private func request(_ format: ClipboardFormat, of copy: Incoming) {
        let item = TransferItem(kind: .clipboard, objectID: copy.clipID, index: format.code)
        let limits = currentLimits()
        Task { [weak self, mesh, limits] in
            let id: UInt64
            do throws(CoreError) {
                if format == .files, let folder = copy.folder {
                    id = try await mesh.receive(item, from: copy.device, into: folder, maxBytes: limits.files)
                } else {
                    id = try await mesh.fetchContent(item, from: copy.device, maxBytes: limits.limit(for: format))
                }
            } catch {
                Log.clipboard.error("Fetching a copy failed: \(String(describing: error), privacy: .public)")
                self?.requestFailed(clipID: copy.clipID)
                return
            }
            self?.registered(id, format: format, clipID: copy.clipID)
        }
    }

    private func registered(_ id: UInt64, format: ClipboardFormat, clipID: UInt64) {
        guard incoming?.clipID == clipID else { return }
        incoming?.pending[id] = format
        if let result = early.removeValue(forKey: id) {
            settle(id, result)
        }
    }

    private func requestFailed(clipID: UInt64) {
        guard incoming?.clipID == clipID else { return }
        incoming?.settled += 1
        finishIfComplete()
    }

    private func settle(_ id: UInt64, _ result: Result<Fetched, CoreError>) {
        guard var copy = incoming else { return }
        guard let format = copy.pending.removeValue(forKey: id) else {
            if copy.settled + copy.pending.count < copy.requests {
                early[id] = result  // its request has not returned yet
            }
            return
        }
        switch result {
        case .success(.content(let data)):
            copy.received[format] = data
        case .success(.files(let urls)):
            copy.files = urls
        case .failure(let error):
            Log.clipboard.error(
                "A copy's \(String(describing: format), privacy: .public) did not arrive: \(String(describing: error), privacy: .public)"
            )
            if error == .tooLarge {
                copy.tooLarge = true
            }
        }
        copy.settled += 1
        incoming = copy
        finishIfComplete()
    }

    private func finishIfComplete() {
        guard let copy = incoming, copy.isComplete else { return }
        guard !copy.received.isEmpty || !copy.files.isEmpty else {
            incoming = nil
            staging.removeAll()
            let name = nameOf(copy.device)
            notice = copy.tooLarge ? .tooLarge(name: name) : .unavailable(name: name)
            return
        }
        Task { [weak self, converter] in
            var tiff: Data?
            if let png = copy.received[.image] {
                tiff = await Task.detached { converter.tiff(fromPNG: png) }.value
            }
            self?.write(copy, tiff: tiff)
        }
    }

    private func write(_ copy: Incoming, tiff: Data?) {
        guard incoming?.clipID == copy.clipID else { return }  // a newer copy replaced it
        incoming = nil
        if marksFiles() {
            FileQuarantine.mark(copy.files)
        }
        let content = RemoteCopy.content(received: copy.received, tiff: tiff, files: copy.files)
        let change = pasteboard.write(content)
        ownChange = change
        seenChange = change
        staging.removeAll(except: copy.files.isEmpty ? nil : copy.folder)
        notice = copy.tooLarge ? .tooLarge(name: nameOf(copy.device)) : nil
    }
}
