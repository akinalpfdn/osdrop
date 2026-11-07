import CoreBridge
import Foundation
import Testing

@testable import Osdrop

/// The shared clipboard on this Mac with a pasteboard and a mesh in memory: what is reported,
/// what is fetched, and what ends up on the pasteboard.
@MainActor
@Suite("Clipboard controller")
struct ClipboardControllerTests {
    private static let pc = makeDeviceID(0x32)

    @Test("A copy on this Mac is reported with its formats while a member is connected")
    func reportsLocalCopies() async throws {
        let fixture = ClipboardFixture()
        fixture.pasteboard.copy([PasteboardTypes.text: Data("hi".utf8), PasteboardTypes.html: Data("<b>hi</b>".utf8)])
        fixture.controller.poll()
        try await waitUntil { fixture.mesh.copies.count == 1 }
        #expect(
            fixture.mesh.copies[0].contents == [
                ClipboardContent(format: .text, data: Data("hi".utf8)),
                ClipboardContent(format: .html, data: Data("<b>hi</b>".utf8)),
            ])
        fixture.controller.poll()  // nothing changed
        #expect(fixture.mesh.copies.count == 1)
    }

    @Test("Turning sharing off withdraws this Mac's copy")
    func withdrawsWhenTurnedOff() async throws {
        let fixture = ClipboardFixture()
        fixture.pasteboard.copy([PasteboardTypes.text: Data("hi".utf8)])
        fixture.controller.poll()
        try await waitUntil { fixture.mesh.copies.count == 1 }
        try await Task.sleep(for: .milliseconds(50))  // the clip ID comes back on the main actor
        fixture.enabled = false
        fixture.shared = false  // what the group's closure says once sharing is off
        fixture.controller.poll()
        try await waitUntil { fixture.mesh.withdrawals == [1] }
        fixture.controller.poll()
        try await Task.sleep(for: .milliseconds(50))
        #expect(fixture.mesh.withdrawals == [1], "withdrawn once")
    }

    @Test("Concealed copies, and copies while nobody is connected, are never read")
    func keepsCopiesHome() async throws {
        let fixture = ClipboardFixture()
        fixture.shared = false
        fixture.pasteboard.copy([PasteboardTypes.text: Data("lonely".utf8)])
        fixture.controller.poll()
        fixture.shared = true
        fixture.pasteboard.copy(
            [PasteboardTypes.text: Data("hunter2".utf8)], extraTypes: ["org.nspasteboard.ConcealedType"])
        fixture.controller.poll()
        try await Task.sleep(for: .milliseconds(50))
        #expect(fixture.pasteboard.readCount == 0)
        #expect(fixture.mesh.copies.isEmpty)
    }

    @Test("A TIFF-only image travels as PNG; formats over their limit stay behind")
    func convertsAndLimits() async throws {
        let fixture = ClipboardFixture(limits: ClipboardLimits(text: 4, image: 100, files: 100))
        let tiff = Data([1, 2, 3])
        fixture.pasteboard.copy([PasteboardTypes.text: Data("too long".utf8), PasteboardTypes.tiff: tiff])
        fixture.controller.poll()
        try await waitUntil { fixture.mesh.copies.count == 1 }
        #expect(fixture.mesh.copies[0].contents == [ClipboardContent(format: .image, data: Data("png:".utf8) + tiff)])
    }

    @Test("Another computer's copy is written once all of it arrived, and not reported back")
    func writesRemoteCopies() async throws {
        let fixture = ClipboardFixture()
        let png = Data([9, 9])
        fixture.controller.handle(
            .clipboardChanged(
                Self.pc, clipID: 7,
                formats: [AnnouncedFormat(format: .text, byteCount: 5), AnnouncedFormat(format: .image, byteCount: 2)]))
        try await waitUntil { fixture.mesh.fetches.count == 2 }
        let text = try #require(fixture.mesh.fetches.first { $0.item.index == ClipboardFormat.text.code })
        let image = try #require(fixture.mesh.fetches.first { $0.item.index == ClipboardFormat.image.code })
        #expect(text.item == TransferItem(kind: .clipboard, objectID: 7, index: ClipboardFormat.text.code))
        #expect(text.device == Self.pc)
        fixture.controller.handle(.contentReceived(id: text.id, .success(Data("hello".utf8))))
        #expect(fixture.pasteboard.written.isEmpty, "the image is still on its way")
        fixture.controller.handle(.contentReceived(id: image.id, .success(png)))
        try await waitUntil { fixture.pasteboard.written.count == 1 }
        #expect(
            fixture.pasteboard.written[0]
                == PasteboardContent(
                    data: [
                        PasteboardTypes.text: Data("hello".utf8), PasteboardTypes.png: png,
                        PasteboardTypes.tiff: Data("tiff:".utf8) + png,
                    ], fileURLs: []))
        fixture.controller.poll()
        try await Task.sleep(for: .milliseconds(50))
        #expect(fixture.mesh.copies.isEmpty)
        #expect(fixture.controller.notice == nil)
    }

    @Test("A result that arrives before its request returns is kept")
    func keepsEarlyResults() async throws {
        let fixture = ClipboardFixture()
        fixture.mesh.deliverEarly { id in
            fixture.controller.handle(.contentReceived(id: id, .success(Data("fast".utf8))))
        }
        fixture.controller.handle(
            .clipboardChanged(Self.pc, clipID: 8, formats: [AnnouncedFormat(format: .text, byteCount: 4)]))
        try await waitUntil { fixture.pasteboard.written.count == 1 }
        #expect(fixture.pasteboard.written[0].data[PasteboardTypes.text] == Data("fast".utf8))
    }

    @Test("Files within the limit arrive in staging and are pasted as file URLs")
    func receivesFiles() async throws {
        let fixture = ClipboardFixture()
        fixture.controller.handle(
            .clipboardChanged(Self.pc, clipID: 9, formats: [AnnouncedFormat(format: .files, byteCount: 50)]))
        try await waitUntil { fixture.mesh.receives.count == 1 }
        let receive = fixture.mesh.receives[0]
        #expect(receive.folder.path.hasPrefix(fixture.staging.root.path))
        #expect(receive.maxBytes == 100)
        let file = receive.folder.appending(path: "report.pdf")
        fixture.controller.handle(
            .transferFinished(id: receive.id, .success(ReceivedItem(items: [file], skippedLinks: 0))))
        try await waitUntil { fixture.pasteboard.written.count == 1 }
        #expect(fixture.pasteboard.written[0].fileURLs == [file])
    }

    @Test("A copy too large or out of reach leaves a notice")
    func notices() async throws {
        let fixture = ClipboardFixture()
        fixture.controller.handle(
            .clipboardChanged(Self.pc, clipID: 10, formats: [AnnouncedFormat(format: .files, byteCount: 10_000)]))
        #expect(fixture.controller.notice == .tooLarge(name: "PC"))
        #expect(fixture.mesh.receives.isEmpty)

        fixture.controller.handle(
            .clipboardChanged(Self.pc, clipID: 11, formats: [AnnouncedFormat(format: .text, byteCount: 3)]))
        try await waitUntil { fixture.mesh.fetches.count == 1 }
        fixture.controller.handle(.contentReceived(id: fixture.mesh.fetches[0].id, .failure(.connectionClosed)))
        #expect(fixture.controller.notice == .unavailable(name: "PC"))
        #expect(fixture.pasteboard.written.isEmpty)
        fixture.controller.dismissNotice()
        #expect(fixture.controller.notice == nil)
    }

    @Test("A newer copy replaces one that is still being fetched")
    func newerCopyWins() async throws {
        let fixture = ClipboardFixture()
        fixture.controller.handle(
            .clipboardChanged(Self.pc, clipID: 12, formats: [AnnouncedFormat(format: .text, byteCount: 3)]))
        try await waitUntil { fixture.mesh.fetches.count == 1 }
        fixture.controller.handle(
            .clipboardChanged(Self.pc, clipID: 13, formats: [AnnouncedFormat(format: .text, byteCount: 3)]))
        try await waitUntil { fixture.mesh.fetches.count == 2 }
        fixture.controller.handle(.contentReceived(id: fixture.mesh.fetches[0].id, .success(Data("old".utf8))))
        fixture.controller.handle(.contentReceived(id: fixture.mesh.fetches[1].id, .success(Data("new".utf8))))
        try await waitUntil { fixture.pasteboard.written.count == 1 }
        try await Task.sleep(for: .milliseconds(50))
        #expect(fixture.pasteboard.written.map { $0.data[PasteboardTypes.text] } == [Data("new".utf8)])
    }

    private func waitUntil(_ condition: @MainActor () -> Bool) async throws {
        for _ in 0..<200 where !condition() {
            try await Task.sleep(for: .milliseconds(10))
        }
        #expect(condition())
    }
}

private func makeDeviceID(_ byte: UInt8) -> DeviceID {
    guard let id = DeviceID(bytes: [UInt8](repeating: byte, count: 16)) else {
        preconditionFailure("16 bytes make a device ID")
    }
    return id
}

@MainActor
private final class ClipboardFixture {
    let pasteboard = FakePasteboard()
    let mesh = ClipboardMesh()
    let staging = ClipboardStaging(
        root: FileManager.default.temporaryDirectory.appending(path: "osdrop-clipboard-tests-\(UUID())"))
    var shared = true
    var enabled = true
    let controller: ClipboardController

    init(limits: ClipboardLimits = ClipboardLimits(text: 100, image: 100, files: 100)) {
        weak var box: ClipboardFixture?
        controller = ClipboardController(
            mesh: mesh, pasteboard: pasteboard, staging: staging, converter: FakeImageConverter(), limits: { limits },
            isEnabled: { box?.enabled ?? false }, isShared: { box?.shared ?? false }, nameOf: { _ in "PC" })
        box = self
    }

    deinit {
        try? FileManager.default.removeItem(at: staging.root)
    }
}

/// Records the clipboard's commands; results are delivered by the tests as mesh events.
private final class ClipboardMesh: MeshDriving, @unchecked Sendable {
    struct Fetch {
        let id: UInt64
        let item: TransferItem
        let device: DeviceID
    }

    struct Receive {
        let id: UInt64
        let folder: URL
        let maxBytes: UInt64
    }

    let events = AsyncStream<MeshEvent> { _ in }
    let deviceID = makeDeviceID(0x31)
    let listenPort: UInt16 = 1

    // Input control is not driven here.
    let control = AsyncStream<ControlEvent> { _ in }
    func input(_ input: ControlInput, ageMicroseconds: UInt64) {}
    func returnHome(inputLost: Bool) {}
    func lockInput(_ locked: Bool) {}
    func setUnavailable(_ reason: ControlUnavailability?) {}
    func cursorMoved(to position: CoordPoint) {}
    func setInputSettings(_ settings: InputSettings) {}
    func setPointerSpeed(_ permille: UInt32, for device: DeviceID) {}
    func setCapsLock(_ on: Bool) {}
    func dragStarted(_ files: [DraggedFile]) -> UInt32? { nil }
    func dragPromised() {}
    func dragStopped() {}
    func dropFinished(_ dragID: UInt32, _ outcome: DropOutcome) {}
    let transferWatch: any TransferFollowing = NoTransfers()
    func setPermissions(_ permissions: MemberPermissions, for device: DeviceID) async throws(CoreError) {}
    func layout() async throws(CoreError) -> [LayoutComputer] { [] }
    func setLayout(_ placements: [Placement]) async throws(CoreError) {}
    func withdraw(_ kind: TransferItem.Kind, objectID: UInt64) async throws(CoreError) {
        lock.withLock { withdrawn.append(objectID) }
    }
    func cancelTransfer(_ id: UInt64) async { lock.withLock { cancelled.append(id) } }
    var withdrawals: [UInt64] { lock.withLock { withdrawn } }
    var cancellations: [UInt64] { lock.withLock { cancelled } }
    private let lock = NSLock()
    private var nextID: UInt64 = 1
    private var copied: [(contents: [ClipboardContent], files: [URL])] = []
    private var fetched: [Fetch] = []
    private var received: [Receive] = []
    private var withdrawn: [UInt64] = []
    private var cancelled: [UInt64] = []
    private var early: (@MainActor @Sendable (UInt64) -> Void)?

    /// Delivers each fetch's result before the fetch returns, as a fast network can.
    func deliverEarly(_ deliver: @escaping @MainActor @Sendable (UInt64) -> Void) {
        lock.withLock { early = deliver }
    }

    var copies: [(contents: [ClipboardContent], files: [URL])] { lock.withLock { copied } }
    var fetches: [Fetch] { lock.withLock { fetched } }
    var receives: [Receive] { lock.withLock { received } }

    func clipboardCopied(_ contents: [ClipboardContent], files: [URL]) async throws(CoreError) -> UInt64 {
        lock.withLock {
            copied.append((contents, files))
            return 1
        }
    }

    func fetchContent(
        _ item: TransferItem, from device: DeviceID, maxBytes: UInt64
    ) async throws(CoreError)
        -> UInt64
    {
        let id = lock.withLock {
            defer { nextID += 1 }
            fetched.append(Fetch(id: nextID, item: item, device: device))
            return nextID
        }
        if let deliver = lock.withLock({ early }) {
            await deliver(id)
        }
        return id
    }

    func receive(
        _ item: TransferItem, from device: DeviceID, into folder: URL, maxBytes: UInt64
    )
        async throws(CoreError) -> UInt64
    {
        lock.withLock {
            defer { nextID += 1 }
            received.append(Receive(id: nextID, folder: folder, maxBytes: maxBytes))
            return nextID
        }
    }

    func groupID() async throws(CoreError) -> GroupID? { nil }
    func peerDiscovered(_ device: DeviceID, group: GroupID?, host: String, port: UInt16) async throws(CoreError) {}
    func peerLost(_ device: DeviceID) async throws(CoreError) {}
    func pair(host: String, port: UInt16) async throws(CoreError) -> UInt64 { 0 }
    func confirmPairing(_ connection: UInt64, accepted: Bool) async throws(CoreError) {}
    func members() async throws(CoreError) -> [GroupMember] { [] }
    func revoke(_ device: DeviceID) async throws(CoreError) {}
    func leaveGroup() async throws(CoreError) {}
    func setScreens(_ monitors: [ScreenMonitor]) async throws(CoreError) {}
    func closeAll(sleeping: Bool) {}
}
