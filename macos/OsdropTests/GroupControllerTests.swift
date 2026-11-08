import CoreBridge
import CoreGraphics
import Foundation
import Testing

@testable import Osdrop

/// The group on this Mac with fake services: what reaches the mesh, what is stored and advertised,
/// and how a pairing looks in the window.
@MainActor
@Suite("Group controller")
struct GroupControllerTests {
    private let fixture = GroupFixture()

    @Test("Starting advertises this computer with its group and reports its screens")
    func startAdvertisesAndReports() async throws {
        fixture.mesh.setGroup(GroupFixture.group)
        let controller = fixture.start()
        try await waitUntil { fixture.discovery.advertisements.last?.groupID == GroupFixture.group }
        let advertised = try #require(fixture.discovery.advertisements.last)
        #expect(advertised.deviceID == fixture.mesh.deviceID)
        #expect(advertised.name == "Test Mac")
        #expect(fixture.discovery.ports.last == FakeMesh.port)
        #expect(fixture.settings.listenPort() == FakeMesh.port)
        try await waitUntil { fixture.mesh.calls.contains(.setScreens(1)) }
        #expect(controller.isRunning)
    }

    @Test("Discovered computers reach the mesh; members and this computer are not offered")
    func discoveryFeedsTheMesh() async throws {
        let controller = fixture.start()
        fixture.mesh.setMembers([
            GroupFixture.member(fixture.mesh.deviceID, isSelf: true), GroupFixture.member(GroupFixture.pc),
        ])
        fixture.discovery.found(GroupFixture.device(fixture.mesh.deviceID, name: "Test Mac"))
        fixture.discovery.found(GroupFixture.device(GroupFixture.laptop, name: "Laptop"))
        fixture.discovery.found(GroupFixture.device(GroupFixture.pc, name: "PC", group: GroupFixture.group))
        try await waitUntil { controller.members.count == 2 && fixture.mesh.discovered.count == 2 }
        #expect(fixture.mesh.discovered == [GroupFixture.laptop, GroupFixture.pc])
        try await waitUntil { controller.nearby.map(\.id) == [GroupFixture.laptop] }

        fixture.discovery.lost(GroupFixture.laptop)
        try await waitUntil { controller.nearby.isEmpty }
        #expect(fixture.mesh.lost == [GroupFixture.laptop])
    }

    @Test("A pairing shows the code, sends the answer and ends when the computer connects")
    func pairingFlow() async throws {
        let controller = fixture.start()
        fixture.discovery.found(GroupFixture.device(GroupFixture.laptop, name: "Laptop"))
        try await waitUntil { controller.nearby.count == 1 }
        controller.pair(with: try #require(controller.nearby.first))
        #expect(controller.pairing == .dialing(name: "Laptop"))
        try await waitUntil { fixture.mesh.calls.contains(.pair(host: "laptop.local.", port: 4242)) }

        fixture.mesh.send(
            .pairingCode(connection: 5, code: 42_917, peer: GroupFixture.peer(GroupFixture.laptop, name: "Laptop")))
        try await waitUntil { controller.pairing == .confirm(connection: 5, code: 42_917, name: "Laptop") }
        controller.answerPairing(accepted: true)
        #expect(controller.pairing == .waiting(code: 42_917, name: "Laptop"))
        try await waitUntil { fixture.mesh.calls.contains(.confirm(5, accepted: true)) }

        fixture.mesh.send(.peerConnected(GroupFixture.peer(GroupFixture.laptop, name: "Laptop"), features: .all))
        try await waitUntil { controller.pairing == nil }
    }

    @Test("The pairing window stays in front while Osdrop is in the background, until the pairing ends")
    func pairingWindowStaysInFront() async throws {
        let controller = fixture.start()
        let presenter = PairingWindowPresenter(controller: controller)
        fixture.mesh.send(.pairingCode(connection: 9, code: 7, peer: GroupFixture.peer(GroupFixture.pc, name: "PC")))
        try await waitUntil { presenter.window != nil }
        let window = try #require(presenter.window)
        #expect(!window.hidesOnDeactivate, "the menu closes and Osdrop goes to the background at once")
        #expect(window.styleMask.contains(.nonactivatingPanel))
        #expect(window.frame.width > 0 && window.frame.height > 0)
        controller.dismissPairing()
        try await waitUntil { presenter.window == nil }
    }

    @Test("A computer that asks to pair gets an answer; a refused pairing says why")
    func incomingAndRefusedPairings() async throws {
        let controller = fixture.start()
        fixture.mesh.send(.pairingCode(connection: 9, code: 7, peer: GroupFixture.peer(GroupFixture.pc, name: "PC")))
        try await waitUntil { controller.pairing == .confirm(connection: 9, code: 7, name: "PC") }
        controller.dismissPairing()
        #expect(controller.pairing == nil)
        try await waitUntil { fixture.mesh.calls.contains(.confirm(9, accepted: false)) }

        fixture.mesh.send(.pairingCode(connection: 10, code: 8, peer: GroupFixture.peer(GroupFixture.pc, name: "PC")))
        fixture.mesh.send(.pairingFailed(connection: 10, reason: .inAnotherGroup))
        try await waitUntil { controller.pairing == .failed(name: "PC", reason: .inAnotherGroup) }
        #expect(PairingFailure(.pairingRejected) == .rejected)
        #expect(PairingFailure(.connectionClosed) == .unreachable)
    }

    @Test("A computer is blocked from its code or the member list, hidden nearby and unblocked")
    func blocksAndUnblocks() async throws {
        let controller = fixture.start()
        fixture.mesh.send(.pairingCode(connection: 9, code: 7, peer: GroupFixture.peer(GroupFixture.pc, name: "PC")))
        try await waitUntil { controller.pairing == .confirm(connection: 9, code: 7, name: "PC") }
        controller.blockPairing()
        #expect(controller.pairing == nil)
        try await waitUntil { fixture.mesh.calls.contains(.block(GroupFixture.pc)) }

        let laptop = GroupFixture.member(GroupFixture.laptop)
        controller.block(laptop)
        try await waitUntil { fixture.mesh.calls.contains(.block(GroupFixture.laptop)) }
        controller.block(GroupFixture.member(fixture.mesh.deviceID, isSelf: true))

        let blockedPC = BlockedDevice(deviceID: GroupFixture.pc, platform: .windows, name: "PC")
        fixture.mesh.setBlocked([blockedPC])
        fixture.discovery.found(GroupFixture.device(GroupFixture.pc, name: "PC"))
        fixture.discovery.found(GroupFixture.device(GroupFixture.laptop, name: "Laptop"))
        fixture.mesh.send(.groupChanged(state: [1], groupID: GroupFixture.group))
        try await waitUntil {
            controller.blocked == [blockedPC] && controller.nearby.map(\.id) == [GroupFixture.laptop]
        }

        controller.unblock(blockedPC)
        try await waitUntil { fixture.mesh.calls.contains(.unblock(GroupFixture.pc)) }
        #expect(!fixture.mesh.calls.contains(.block(fixture.mesh.deviceID)), "never this computer")
        #expect(PairingFailure(.notTrusted) == .refused)
    }

    @Test("Group changes are stored and advertised; the arrangement is stored")
    func changesAreStored() async throws {
        let controller = fixture.start()
        fixture.mesh.send(.groupChanged(state: [1, 2, 3], groupID: GroupFixture.group))
        fixture.mesh.send(.layoutChanged(state: [9]))
        try await waitUntil { fixture.discovery.advertisements.last?.groupID == GroupFixture.group }
        #expect(fixture.keyStore.storedGroup == [1, 2, 3])
        #expect(controller.groupID == GroupFixture.group)
        try await waitUntil { fixture.settings.layoutState() == [9] }

        fixture.mesh.send(.leftGroup(reason: .notTrusted))
        try await waitUntil { controller.wasRemoved }
    }

    @Test("A group stored by a newer Osdrop stops the start and is kept")
    func newerGroupStopsTheStart() {
        fixture.keyStore.storedGroup = [7, 7]
        fixture.failure = { _ in .unsupportedVersion }
        let controller = fixture.start()
        #expect(controller.startFailure == .newerVersion)
        #expect(!controller.isRunning)
        #expect(fixture.keyStore.storedGroup == [7, 7])
    }

    @Test("A damaged arrangement is dropped and the mesh starts without it")
    func damagedLayoutStartsOver() {
        fixture.settings.saveLayoutState([0xFF])
        fixture.failure = { configuration in configuration.layoutState == nil ? nil : .badMagic }
        let controller = fixture.start()
        #expect(controller.isRunning)
        #expect(controller.startFailure == nil)
    }

    @Test("Names are cut to 64 bytes without control characters")
    func deviceNames() {
        #expect(DeviceName.clean("Akın's MacBook\u{7}") == "Akın's MacBook")
        #expect(DeviceName.clean(String(repeating: "ğ", count: 40)).utf8.count == 64)
        #expect(DeviceName.clean("  ") == "Mac")
        #expect(PairingCode.display(42_917) == "042 917")
    }

    @Test("A name too long for the wire is cut, and renaming recovers a mesh that failed to start")
    func renameKeepsTheMeshStartable() async throws {
        let fixture = GroupFixture()
        let names = Locked<[String]>([])
        fixture.failure = { configuration in
            names.withLock { $0.append(configuration.name) }
            return nil
        }
        let controller = fixture.start()
        controller.rename(to: String(repeating: "Çalışma ", count: 12))
        let used = try #require(names.withLock { $0.last })
        #expect(used.utf8.count <= DeviceName.maximumBytes)
        #expect(controller.isRunning)

        let failing = GroupFixture()
        failing.failure = { _ in .invalidString }
        let stopped = failing.start()
        #expect(stopped.startFailure == .mesh)
        failing.failure = { _ in nil }
        stopped.rename(to: "Desk")
        #expect(stopped.startFailure == nil)
        #expect(stopped.isRunning)
    }

    private func waitUntil(_ condition: @MainActor () -> Bool) async throws {
        for _ in 0..<200 {
            if condition() { return }
            try await Task.sleep(for: .milliseconds(10))
        }
        #expect(condition())
    }
}

/// The fake services, wired as the app wires the real ones.
@MainActor
final class GroupFixture {
    nonisolated static let laptop = id(0x31)
    nonisolated static let pc = id(0x32)
    nonisolated static let group = GroupID(bytes: [UInt8](repeating: 0x47, count: 16))

    let mesh = FakeMesh()
    let discovery = FakeDiscovery()
    let keyStore = MemoryKeyStore()
    let settings = MemorySettings()
    let screens = FakeScreens()
    /// Makes the mesh fail to start with this error for a configuration, or not (`nil`).
    var failure: @Sendable (Mesh.Configuration) -> CoreError? {
        get { failureBox.withLock { $0 } }
        set { failureBox.withLock { $0 = newValue } }
    }
    /// Read at every start, so a test can change it between starts.
    private let failureBox = Locked<@Sendable (Mesh.Configuration) -> CoreError?>({ _ in nil })

    func start() -> GroupController {
        let mesh = self.mesh
        let failure = failureBox
        let services = GroupServices(
            keyStore: keyStore, settings: settings, discovery: discovery, localNetwork: FakeLocalNetwork(),
            makeMesh: { configuration throws(CoreError) in
                if let error = failure.withLock({ $0 })(configuration) { throw error }
                return mesh
            }, deviceName: "Test Mac", clipboard: .fake(), input: .fake(), notifier: FakeNotifier())
        let defaults = UserDefaults(suiteName: "osdrop-tests-\(UUID())") ?? .standard
        let controller = GroupController(
            services: services, screens: screens, settings: AppSettings(defaults: defaults))
        controller.start()
        return controller
    }

    nonisolated static func id(_ byte: UInt8) -> DeviceID {
        guard let id = DeviceID(bytes: [UInt8](repeating: byte, count: 16)) else {
            preconditionFailure("16 bytes always make a device ID")
        }
        return id
    }

    static func device(_ id: DeviceID, name: String, group: GroupID? = nil) -> DiscoveredDevice {
        DiscoveredDevice(
            label: id.hex, advertisement: Advertisement(deviceID: id, groupID: group, name: name),
            host: id == laptop ? "laptop.local." : "pc.local.", port: id == laptop ? 4242 : 4343)
    }

    static func peer(_ id: DeviceID, name: String) -> MeshPeer {
        MeshPeer(deviceID: id, publicKey: [], platform: .macOS, protocolMinor: 0, groupID: nil, name: name)
    }

    static func member(_ id: DeviceID, isSelf: Bool = false) -> GroupMember {
        GroupMember(
            deviceID: id, publicKey: [], platform: .macOS, name: isSelf ? "Test Mac" : "Member",
            permissions: .all, isSelf: isSelf, isConnected: !isSelf)
    }
}

final class FakeMesh: MeshDriving, @unchecked Sendable {
    enum Call: Equatable {
        case pair(host: String, port: UInt16)
        case confirm(UInt64, accepted: Bool)
        case setScreens(Int)
        case revoke(DeviceID)
        case block(DeviceID)
        case unblock(DeviceID)
        case leave
        case permissions(MemberPermissions, DeviceID)
        case setLayout([Placement])
    }

    static let port: UInt16 = 50_000

    let events: AsyncStream<MeshEvent>
    let deviceID = GroupFixture.id(0x30)
    let listenPort = FakeMesh.port

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
    private let continuation: AsyncStream<MeshEvent>.Continuation
    private let lock = NSLock()
    private var group: GroupID?
    private var memberList: [GroupMember] = []
    private var blockedList: [BlockedDevice] = []
    private var computers: [LayoutComputer] = []
    private var recorded: [Call] = []
    private var discoveredIDs: [DeviceID] = []
    private var lostIDs: [DeviceID] = []

    init() {
        (events, continuation) = AsyncStream.makeStream(of: MeshEvent.self)
    }

    var calls: [Call] { lock.withLock { recorded } }
    var discovered: [DeviceID] { lock.withLock { discoveredIDs } }
    var lost: [DeviceID] { lock.withLock { lostIDs } }

    func send(_ event: MeshEvent) { continuation.yield(event) }
    func setGroup(_ id: GroupID?) { lock.withLock { group = id } }
    func setMembers(_ members: [GroupMember]) { lock.withLock { memberList = members } }
    func setBlocked(_ devices: [BlockedDevice]) { lock.withLock { blockedList = devices } }

    func groupID() async throws(CoreError) -> GroupID? { lock.withLock { group } }
    func peerDiscovered(_ device: DeviceID, group: GroupID?, host: String, port: UInt16) async throws(CoreError) {
        lock.withLock { discoveredIDs.append(device) }
    }
    func peerLost(_ device: DeviceID) async throws(CoreError) { lock.withLock { lostIDs.append(device) } }
    func pair(host: String, port: UInt16) async throws(CoreError) -> UInt64 {
        lock.withLock { recorded.append(.pair(host: host, port: port)) }
        return 5
    }
    func confirmPairing(_ connection: UInt64, accepted: Bool) async throws(CoreError) {
        lock.withLock { recorded.append(.confirm(connection, accepted: accepted)) }
    }
    func members() async throws(CoreError) -> [GroupMember] { lock.withLock { memberList } }
    func revoke(_ device: DeviceID) async throws(CoreError) { lock.withLock { recorded.append(.revoke(device)) } }
    func leaveGroup() async throws(CoreError) { lock.withLock { recorded.append(.leave) } }
    func block(_ device: DeviceID) async throws(CoreError) { lock.withLock { recorded.append(.block(device)) } }
    func unblock(_ device: DeviceID) async throws(CoreError) { lock.withLock { recorded.append(.unblock(device)) } }
    func blocked() async throws(CoreError) -> [BlockedDevice] { lock.withLock { blockedList } }
    func setScreens(_ monitors: [ScreenMonitor]) async throws(CoreError) {
        lock.withLock { recorded.append(.setScreens(monitors.count)) }
    }
    func closeAll(sleeping: Bool) {}
    func setPermissions(_ permissions: MemberPermissions, for device: DeviceID) async throws(CoreError) {
        lock.withLock { recorded.append(.permissions(permissions, device)) }
    }
    func layout() async throws(CoreError) -> [LayoutComputer] { lock.withLock { computers } }
    func withdraw(_ kind: TransferItem.Kind, objectID: UInt64) async throws(CoreError) {}
    func cancelTransfer(_ id: UInt64) async {}
    func setComputers(_ layout: [LayoutComputer]) { lock.withLock { computers = layout } }
    func setLayout(_ placements: [Placement]) async throws(CoreError) {
        lock.withLock { recorded.append(.setLayout(placements)) }
    }
    func clipboardCopied(_ contents: [ClipboardContent], files: [URL]) async throws(CoreError) -> UInt64 { 1 }
    func fetchContent(
        _ item: TransferItem, from device: DeviceID, maxBytes: UInt64
    ) async throws(CoreError)
        -> UInt64
    { 1 }
    func receive(
        _ item: TransferItem, from device: DeviceID, into folder: URL, maxBytes: UInt64
    )
        async throws(CoreError) -> UInt64
    { 1 }
}

final class FakeDiscovery: PeerDiscovering, @unchecked Sendable {
    private struct Registration: Advertising {
        func cancel() {}
    }

    private let lock = NSLock()
    private var registered: [(Advertisement, UInt16)] = []
    private var continuation: AsyncStream<DiscoveryEvent>.Continuation?

    var advertisements: [Advertisement] { lock.withLock { registered.map(\.0) } }
    var ports: [UInt16] { lock.withLock { registered.map(\.1) } }

    func advertise(_ advertisement: Advertisement, port: UInt16) throws(DiscoveryError) -> any Advertising {
        lock.withLock { registered.append((advertisement, port)) }
        return Registration()
    }

    func browse() -> AsyncStream<DiscoveryEvent> {
        let (stream, continuation) = AsyncStream.makeStream(of: DiscoveryEvent.self)
        lock.withLock { self.continuation = continuation }
        return stream
    }

    func found(_ device: DiscoveredDevice) { lock.withLock { continuation }?.yield(.found(device)) }
    func lost(_ id: DeviceID) { lock.withLock { continuation }?.yield(.lost(label: id.hex)) }
}

final class MemoryKeyStore: KeyStoring, @unchecked Sendable {
    private let lock = NSLock()
    private let stored: DeviceIdentity?
    private var group: [UInt8]?

    init() {
        stored = try? DeviceIdentity.generate()  // the system generator does not fail in tests
    }

    var storedGroup: [UInt8]? {
        get { lock.withLock { group } }
        set { lock.withLock { group = newValue } }
    }

    func identity() throws(KeyStoreError) -> DeviceIdentity {
        guard let stored else { throw .damaged }
        return stored
    }
    func groupState() throws(KeyStoreError) -> [UInt8]? { storedGroup }
    func saveGroupState(_ state: [UInt8]) throws(KeyStoreError) { storedGroup = state }
}

final class MemorySettings: SettingsStoring, @unchecked Sendable {
    private let lock = NSLock()
    private var layout: [UInt8]?
    private var port: UInt16 = 0

    func layoutState() -> [UInt8]? { lock.withLock { layout } }
    func saveLayoutState(_ state: [UInt8]) { lock.withLock { layout = state } }
    func listenPort() -> UInt16 { lock.withLock { port } }
    func saveListenPort(_ port: UInt16) { lock.withLock { self.port = port } }
}

struct FakeLocalNetwork: LocalNetworkChecking {
    func accessUpdates() -> AsyncStream<LocalNetworkAccess> {
        AsyncStream { $0.yield(.allowed) }
    }
}

@MainActor
final class FakeScreens: ScreenDescribing {
    var onChange: (() -> Void)?

    func screens() -> [ScreenDescription] {
        [
            ScreenDescription(
                id: "0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0", displayID: 1,
                frame: CGRect(x: 0, y: 0, width: 1470, height: 956), pixelWidth: 2940, pixelHeight: 1912,
                isMain: true)
        ]
    }
}
