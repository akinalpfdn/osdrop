import CoreBridge
import Foundation

/// The mesh as the app drives it. Core's commands wait for its I/O thread, so the real one runs
/// them on an actor, off the main thread.
protocol MeshDriving: Sendable {
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
    func setScreens(_ monitors: [ScreenMonitor]) async throws(CoreError)
    /// Returns at once: core queues the Byes, also while the app is quitting.
    func closeAll(sleeping: Bool)
    func clipboardCopied(_ contents: [ClipboardContent], files: [URL]) async throws(CoreError) -> UInt64
    func fetchContent(
        _ item: TransferItem, from device: DeviceID, maxBytes: UInt64
    ) async throws(CoreError)
        -> UInt64
    func receive(
        _ item: TransferItem, from device: DeviceID, into folder: URL, maxBytes: UInt64
    )
        async throws(CoreError) -> UInt64
}

actor CoreMesh: MeshDriving {
    nonisolated let events: AsyncStream<MeshEvent>
    nonisolated let deviceID: DeviceID
    nonisolated let listenPort: UInt16
    private let mesh: Mesh

    init(_ configuration: Mesh.Configuration) throws(CoreError) {
        let mesh = try Mesh(configuration)
        self.mesh = mesh
        events = mesh.events
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

    func setScreens(_ monitors: [ScreenMonitor]) throws(CoreError) { try mesh.setScreens(monitors) }

    nonisolated func closeAll(sleeping: Bool) { mesh.closeAll(sleeping: sleeping) }

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
