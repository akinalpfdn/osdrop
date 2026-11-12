import CoreBridge
import Foundation

/// Input control as the app drives it (docs/PROTOCOL.md "Input control messages"). Every command
/// returns at once and may be called from any thread, so captured input never waits for an actor.
protocol ControlDriving: Sendable {
    var control: AsyncStream<ControlEvent> { get }
    /// Physical input on this Mac, captured `ageMicroseconds` ago.
    func input(_ input: ControlInput, ageMicroseconds: UInt64)
    func returnHome(inputLost: Bool)
    func lockInput(_ locked: Bool)
    func setUnavailable(_ reason: ControlUnavailability?)
    func cursorMoved(to position: CoordPoint)
    func setInputSettings(_ settings: InputSettings)
    func setPointerSpeed(_ permille: UInt32, for device: DeviceID)
    func setCapsLock(_ on: Bool)
    /// A drag began on this Mac with these files and folders (none: nothing can travel), or the
    /// files of a wanted drag of promised files arrived. Its drag ID; nil without one.
    @discardableResult
    func dragStarted(_ files: [DraggedFile]) -> UInt32?
    /// A drag of promised files began on this Mac.
    func dragPromised()
    func dragStopped()
    /// A drag that came from another computer ended here.
    func dropFinished(_ dragID: UInt32, _ outcome: DropOutcome)
}

/// Follows a transfer by its ID without the main thread (CoreBridge's `TransferWatch`).
protocol TransferFollowing: Sendable {
    func watch(
        _ id: UInt64, progress: @escaping @Sendable (TransferProgress) -> Void,
        finished: @escaping @Sendable (Result<ReceivedItem, CoreError>) -> Void)
}

extension TransferWatch: TransferFollowing {}

/// Fetching items other members offer.
protocol TransferDriving: Sendable {
    var transferWatch: any TransferFollowing { get }
    func receive(
        _ item: TransferItem, from device: DeviceID, into folder: URL, maxBytes: UInt64
    )
        async throws(CoreError) -> UInt64
    func cancelTransfer(_ id: UInt64) async
}

/// The mesh as the app drives it. Core's commands wait for its I/O thread, so the real one runs
/// them on an actor, off the main thread.
protocol MeshDriving: ControlDriving, TransferDriving {
    var events: AsyncStream<MeshEvent> { get }
    var deviceID: DeviceID { get }
    var listenPort: UInt16 { get }

    func groupID() async throws(CoreError) -> GroupID?
    func peerDiscovered(_ device: DeviceID, group: GroupID?, host: String, port: UInt16) async throws(CoreError)
    func peerLost(_ device: DeviceID) async throws(CoreError)
    func pair(host: String, port: UInt16) async throws(CoreError) -> UInt64
    func confirmPairing(_ connection: UInt64, accepted: Bool) async throws(CoreError)
    func members() async throws(CoreError) -> [GroupMember]
    func revoke(_ device: DeviceID) async throws(CoreError)
    func leaveGroup() async throws(CoreError)
    func block(_ device: DeviceID) async throws(CoreError)
    func unblock(_ device: DeviceID) async throws(CoreError)
    func blocked() async throws(CoreError) -> [BlockedDevice]
    func setScreens(_ monitors: [ScreenMonitor]) async throws(CoreError)
    func setPermissions(_ permissions: MemberPermissions, for device: DeviceID) async throws(CoreError)
    func layout() async throws(CoreError) -> [LayoutComputer]
    func setLayout(_ placements: [Placement]) async throws(CoreError)
    func withdraw(_ kind: TransferItem.Kind, objectID: UInt64) async throws(CoreError)
    /// Returns at once: core queues the Byes, also while the app is quitting.
    func closeAll(sleeping: Bool)
    /// Returns at once, like closeAll.
    func resume()
    func clipboardCopied(_ contents: [ClipboardContent], files: [URL]) async throws(CoreError) -> UInt64
    func fetchContent(
        _ item: TransferItem, from device: DeviceID, maxBytes: UInt64
    ) async throws(CoreError)
        -> UInt64
}

actor CoreMesh: MeshDriving {
    nonisolated let events: AsyncStream<MeshEvent>
    nonisolated let control: AsyncStream<ControlEvent>
    nonisolated let transferWatch: any TransferFollowing
    nonisolated let deviceID: DeviceID
    nonisolated let listenPort: UInt16
    private let mesh: Mesh

    init(_ configuration: Mesh.Configuration) throws(CoreError) {
        let mesh = try Mesh(configuration)
        self.mesh = mesh
        events = mesh.events
        control = mesh.control
        transferWatch = mesh.transfers
        deviceID = mesh.deviceID
        listenPort = mesh.listenPort
    }

    func groupID() throws(CoreError) -> GroupID? { try mesh.groupID() }

    func peerDiscovered(_ device: DeviceID, group: GroupID?, host: String, port: UInt16) throws(CoreError) {
        try mesh.peerDiscovered(device, group: group, host: host, port: port)
    }

    func peerLost(_ device: DeviceID) throws(CoreError) { try mesh.peerLost(device) }

    func pair(host: String, port: UInt16) throws(CoreError) -> UInt64 { try mesh.pair(host: host, port: port) }

    func confirmPairing(_ connection: UInt64, accepted: Bool) throws(CoreError) {
        try mesh.confirmPairing(connection, accepted: accepted)
    }

    func members() throws(CoreError) -> [GroupMember] { try mesh.members() }

    func revoke(_ device: DeviceID) throws(CoreError) { try mesh.revoke(device) }

    func leaveGroup() throws(CoreError) { try mesh.leaveGroup() }

    func block(_ device: DeviceID) throws(CoreError) { try mesh.block(device) }

    func unblock(_ device: DeviceID) throws(CoreError) { try mesh.unblock(device) }

    func blocked() throws(CoreError) -> [BlockedDevice] { try mesh.blocked() }

    func setScreens(_ monitors: [ScreenMonitor]) throws(CoreError) { try mesh.setScreens(monitors) }

    func setPermissions(_ permissions: MemberPermissions, for device: DeviceID) throws(CoreError) {
        try mesh.setPermissions(permissions, for: device)
    }

    func layout() throws(CoreError) -> [LayoutComputer] { try mesh.layout() }

    func setLayout(_ placements: [Placement]) throws(CoreError) { try mesh.setLayout(placements) }

    func withdraw(_ kind: TransferItem.Kind, objectID: UInt64) throws(CoreError) {
        try mesh.withdraw(kind, objectID: objectID)
    }

    func cancelTransfer(_ id: UInt64) { mesh.cancelTransfer(id) }

    nonisolated func closeAll(sleeping: Bool) { mesh.closeAll(sleeping: sleeping) }

    nonisolated func resume() { mesh.resume() }

    // The input commands only queue work for core's I/O thread; their only failure is an argument
    // the app built wrong.
    nonisolated func input(_ input: ControlInput, ageMicroseconds: UInt64) {
        report { () throws(CoreError) in try mesh.input(input, ageMicroseconds: ageMicroseconds) }
    }

    nonisolated func returnHome(inputLost: Bool) {
        report { () throws(CoreError) in try mesh.returnHome(inputLost: inputLost) }
    }

    nonisolated func lockInput(_ locked: Bool) {
        report { () throws(CoreError) in try mesh.lockInput(locked) }
    }

    nonisolated func setUnavailable(_ reason: ControlUnavailability?) {
        report { () throws(CoreError) in try mesh.setUnavailable(reason) }
    }

    nonisolated func cursorMoved(to position: CoordPoint) {
        report { () throws(CoreError) in try mesh.cursorMoved(to: position) }
    }

    nonisolated func setInputSettings(_ settings: InputSettings) {
        report { () throws(CoreError) in try mesh.setInputSettings(settings) }
    }

    nonisolated func setPointerSpeed(_ permille: UInt32, for device: DeviceID) {
        report { () throws(CoreError) in try mesh.setPointerSpeed(permille, for: device) }
    }

    nonisolated func setCapsLock(_ on: Bool) {
        report { () throws(CoreError) in try mesh.setCapsLock(on) }
    }

    nonisolated func dragStarted(_ files: [DraggedFile]) -> UInt32? {
        do throws(CoreError) {
            let dragID = try mesh.dragStarted(files)
            return dragID == 0 ? nil : dragID
        } catch {
            Log.input.error("Reporting a drag failed: \(String(describing: error), privacy: .public)")
            return nil
        }
    }

    nonisolated func dragPromised() {
        report { () throws(CoreError) in try mesh.dragPromised() }
    }

    nonisolated func dragStopped() {
        report { () throws(CoreError) in try mesh.dragStopped() }
    }

    nonisolated func dropFinished(_ dragID: UInt32, _ outcome: DropOutcome) {
        report { () throws(CoreError) in try mesh.dropFinished(dragID, outcome) }
    }

    private nonisolated func report(_ command: () throws(CoreError) -> Void) {
        do throws(CoreError) {
            try command()
        } catch {
            Log.input.error("Input command failed: \(String(describing: error), privacy: .public)")
        }
    }

    func clipboardCopied(_ contents: [ClipboardContent], files: [URL]) throws(CoreError) -> UInt64 {
        try mesh.clipboardCopied(contents, files: files)
    }

    func fetchContent(_ item: TransferItem, from device: DeviceID, maxBytes: UInt64) throws(CoreError) -> UInt64 {
        try mesh.fetchContent(item, from: device, maxBytes: maxBytes)
    }

    func receive(
        _ item: TransferItem, from device: DeviceID, into folder: URL, maxBytes: UInt64
    ) throws(CoreError)
        -> UInt64
    {
        try mesh.receive(item, from: device, into: folder, maxBytes: maxBytes)
    }
}
