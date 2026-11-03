import Foundation
import OsdropCore

/// This computer's mesh and group (docs/PROTOCOL.md "Session flow", "Group membership",
/// "Arrangement").
///
/// Core runs the mesh on its own I/O thread and reports through `events`, in order. Commands may be
/// called from any thread; most wait until the I/O thread has run them, so call them from an actor
/// rather than the main thread.
public final class Mesh: Sendable {
    public struct Configuration: Sendable {
        public var identity: DeviceIdentity
        /// 1 to 64 bytes of UTF-8.
        public var name: String
        /// The bytes of the last `.groupChanged`; `nil` the first time.
        public var groupState: [UInt8]?
        /// The bytes of the last `.layoutChanged`; `nil` the first time.
        public var layoutState: [UInt8]?
        public var features: MeshFeatures
        public var maxTransferStreams: UInt8
        /// 0: any. Reusing the previous run's port keeps a Bonjour advertisement right.
        public var listenPort: UInt16

        public init(
            identity: DeviceIdentity, name: String, groupState: [UInt8]? = nil, layoutState: [UInt8]? = nil,
            features: MeshFeatures = .all, maxTransferStreams: UInt8 = 4, listenPort: UInt16 = 0
        ) {
            self.identity = identity
            self.name = name
            self.groupState = groupState
            self.layoutState = layoutState
            self.features = features
            self.maxTransferStreams = maxTransferStreams
            self.listenPort = listenPort
        }
    }

    public let events: AsyncStream<MeshEvent>
    public let deviceID: DeviceID
    public let listenPort: UInt16

    private let handle: MeshHandle
    /// Holds the callbacks' context; must outlive the core mesh.
    private let box: MeshCallbackBox

    /// Starts listening and core's I/O thread. Throws `.unsupportedVersion` for a stored state
    /// written by a newer Osdrop (keep it; do not overwrite it).
    public init(_ configuration: Configuration) throws(CoreError) {
        let (events, continuation) = AsyncStream.makeStream(of: MeshEvent.self)
        let box = MeshCallbackBox(continuation: continuation)
        var created: OpaquePointer?
        let groupState = configuration.groupState ?? []
        let layoutState = configuration.layoutState ?? []
        let result = configuration.identity.secretKey.withUnsafeBufferPointer { secret in
            configuration.name.withCString { name in
                groupState.withUnsafeBufferPointer { group in
                    layoutState.withUnsafeBufferPointer { layout in
                        var config = osdrop_mesh_config()
                        config.secret_key = secret.baseAddress
                        config.device_name = name
                        config.platform = PeerPlatform.current.raw
                        config.group_state = configuration.groupState == nil ? nil : group.baseAddress
                        config.group_state_size = configuration.groupState == nil ? 0 : group.count
                        config.layout_state = configuration.layoutState == nil ? nil : layout.baseAddress
                        config.layout_state_size = configuration.layoutState == nil ? 0 : layout.count
                        config.features = configuration.features.rawValue
                        config.max_transfer_streams = configuration.maxTransferStreams
                        config.listen_port = configuration.listenPort
                        var random = SystemRandom.source
                        var callbacks = box.callbacks
                        return osdrop_mesh_create(&config, &random, &callbacks, &created)
                    }
                }
            }
        }
        if let error = CoreError(result: result) {
            continuation.finish()
            throw error
        }
        guard let created else {
            continuation.finish()
            throw .internalFailure
        }
        var port: UInt16 = 0
        _ = osdrop_mesh_listen_port(created, &port)
        self.events = events
        self.box = box
        self.handle = MeshHandle(created)
        self.deviceID = configuration.identity.id
        self.listenPort = port
    }

    deinit {
        handle.destroy()
        box.continuation.finish()
    }

    /// The group to advertise; `nil` in no group.
    public func groupID() throws(CoreError) -> GroupID? {
        var id = [UInt8](repeating: 0, count: Int(OSDROP_GROUP_ID_SIZE))
        var hasGroup: UInt8 = 0
        try check(handle.withPointer { osdrop_mesh_group_id($0, &id, &hasGroup) })
        return hasGroup == 1 ? GroupID(bytes: id) : nil
    }

    /// From discovery: a computer advertising `group` at `host`:`port`.
    public func peerDiscovered(_ device: DeviceID, group: GroupID?, host: String, port: UInt16) throws(CoreError) {
        try check(
            handle.withPointer { pointer in
                if let group {
                    return osdrop_mesh_peer_discovered(pointer, device.bytes, group.bytes, host, port)
                }
                return osdrop_mesh_peer_discovered(pointer, device.bytes, nil, host, port)
            })
    }

    public func peerLost(_ device: DeviceID) throws(CoreError) {
        try check(handle.withPointer { osdrop_mesh_peer_lost($0, device.bytes) })
    }

    /// Dials `host`:`port` to pair; the result identifies the pairing's events.
    public func pair(host: String, port: UInt16) throws(CoreError) -> UInt64 {
        var connection: UInt64 = 0
        try check(handle.withPointer { osdrop_mesh_pair($0, host, port, &connection) })
        return connection
    }

    /// Answers `.pairingCode` with the local user's decision.
    public func confirmPairing(_ connection: UInt64, accepted: Bool) throws(CoreError) {
        try check(handle.withPointer { osdrop_mesh_confirm_pairing($0, connection, accepted ? 1 : 0) })
    }

    /// The group's members, this computer included.
    public func members() throws(CoreError) -> [GroupMember] {
        var count = 0
        let counted = handle.withPointer { osdrop_mesh_members($0, nil, 0, &count) }
        if counted != OSDROP_ERROR_BUFFER_TOO_SMALL {
            try check(counted)
        }
        var members = [osdrop_member](repeating: osdrop_member(), count: count)
        try check(handle.withPointer { osdrop_mesh_members($0, &members, members.count, &count) })
        return members.prefix(count).map(GroupMember.init)
    }

    /// Removes a member everywhere; it is told it was removed.
    public func revoke(_ device: DeviceID) throws(CoreError) {
        try check(handle.withPointer { osdrop_mesh_revoke($0, device.bytes) })
    }

    public func leaveGroup() throws(CoreError) {
        try check(handle.withPointer { osdrop_mesh_leave_group($0) })
    }

    public func setPermissions(_ permissions: MemberPermissions, for device: DeviceID) throws(CoreError) {
        var raw = permissions.raw
        try check(handle.withPointer { osdrop_mesh_set_permissions($0, device.bytes, &raw) })
    }

    /// Ends every session before sleep (peers see `.peerSleeping`) or quitting.
    public func closeAll(sleeping: Bool) {
        _ = handle.withPointer { osdrop_mesh_close_all($0, sleeping ? 1 : 0) }
    }

    /// This computer's monitors, at start and after every display change.
    public func setScreens(_ monitors: [ScreenMonitor]) throws(CoreError) {
        let raw = monitors.map(\.raw)
        try check(
            handle.withPointer { pointer in
                raw.withUnsafeBufferPointer { osdrop_mesh_set_screens(pointer, $0.baseAddress, $0.count) }
            })
    }

    /// The members whose screens are known, this computer included, with their places.
    public func layout() throws(CoreError) -> [LayoutComputer] {
        var count = 0
        let counted = handle.withPointer { osdrop_mesh_layout($0, nil, 0, &count) }
        if counted != OSDROP_ERROR_BUFFER_TOO_SMALL {
            try check(counted)
        }
        var computers = [osdrop_layout_computer](repeating: osdrop_layout_computer(), count: count)
        try check(handle.withPointer { osdrop_mesh_layout($0, &computers, computers.count, &count) })
        return computers.prefix(count).map(LayoutComputer.init)
    }

    /// A new arrangement, shared with every member; `.invalidLayout` when monitors overlap or a
    /// computer touches no other.
    public func setLayout(_ placements: [Placement]) throws(CoreError) {
        let raw = placements.map(\.raw)
        try check(
            handle.withPointer { pointer in
                raw.withUnsafeBufferPointer { osdrop_mesh_set_layout(pointer, $0.baseAddress, $0.count) }
            })
    }

    private func check(_ result: osdrop_result?) throws(CoreError) {
        guard let result else { throw .invalidState }
        if let error = CoreError(result: result) { throw error }
    }
}

/// The core handle, destroyed exactly once. A lock orders commands against destruction.
private final class MeshHandle: @unchecked Sendable {
    private let lock = NSLock()
    private var pointer: OpaquePointer?

    init(_ pointer: OpaquePointer) { self.pointer = pointer }

    /// `nil` once the mesh is destroyed.
    func withPointer(_ body: (OpaquePointer) -> osdrop_result) -> osdrop_result? {
        lock.lock()
        defer { lock.unlock() }
        return pointer.map(body)
    }

    func destroy() {
        lock.lock()
        let destroyed = pointer
        pointer = nil
        lock.unlock()
        // Outside the lock: destroying joins the I/O thread, whose callbacks never take this lock.
        osdrop_mesh_destroy(destroyed)
    }
}

/// Context of the C callbacks: forwards everything to the event stream.
private final class MeshCallbackBox: Sendable {
    let continuation: AsyncStream<MeshEvent>.Continuation

    init(continuation: AsyncStream<MeshEvent>.Continuation) {
        self.continuation = continuation
    }

    var callbacks: osdrop_mesh_callbacks {
        var callbacks = osdrop_mesh_callbacks()
        callbacks.context = Unmanaged.passUnretained(self).toOpaque()
        callbacks.group_changed = { context, state, size, group in
            let bytes = state.map { Array(UnsafeBufferPointer(start: $0, count: size)) } ?? []
            let id = group.flatMap {
                GroupID(bytes: Array(UnsafeBufferPointer(start: $0, count: Int(OSDROP_GROUP_ID_SIZE))))
            }
            MeshCallbackBox.from(context)?.continuation.yield(.groupChanged(state: bytes, groupID: id))
        }
        callbacks.left_group = { context, reason in
            MeshCallbackBox.from(context)?.continuation.yield(.leftGroup(reason: CoreError(result: reason)))
        }
        callbacks.layout_changed = { context, state, size in
            let bytes = state.map { Array(UnsafeBufferPointer(start: $0, count: size)) } ?? []
            MeshCallbackBox.from(context)?.continuation.yield(.layoutChanged(state: bytes))
        }
        callbacks.pairing_code = { context, connection, code, peer in
            guard let peer = peer?.pointee else { return }
            MeshCallbackBox.from(context)?.continuation.yield(
                .pairingCode(connection: connection, code: code, peer: MeshPeer(peer)))
        }
        callbacks.pairing_failed = { context, connection, reason in
            MeshCallbackBox.from(context)?.continuation.yield(
                .pairingFailed(connection: connection, reason: CoreError(result: reason) ?? .internalFailure))
        }
        callbacks.peer_connected = { context, peer, features in
            guard let peer = peer?.pointee else { return }
            MeshCallbackBox.from(context)?.continuation.yield(
                .peerConnected(MeshPeer(peer), features: MeshFeatures(rawValue: features)))
        }
        callbacks.peer_disconnected = { context, device, reason in
            guard let device else { return }
            MeshCallbackBox.from(context)?.continuation.yield(
                .peerDisconnected(deviceID(device), reason: CoreError(result: reason)))
        }
        callbacks.peer_responsive = { context, device, responsive in
            guard let device else { return }
            MeshCallbackBox.from(context)?.continuation.yield(.peerResponsive(deviceID(device), responsive == 1))
        }
        callbacks.stats = { context, device, stats in
            guard let device, let stats = stats?.pointee else { return }
            MeshCallbackBox.from(context)?.continuation.yield(
                .statistics(deviceID(device), SessionStatistics(stats)))
        }
        callbacks.log = { context, level, message in
            let text = message.map { String(cString: $0) } ?? ""
            MeshCallbackBox.from(context)?.continuation.yield(.log(MeshLogLevel(level), text))
        }
        return callbacks
    }

    /// Core passes back the context it was given; Mesh keeps the box alive.
    static func from(_ context: UnsafeMutableRawPointer?) -> MeshCallbackBox? {
        context.map { Unmanaged<MeshCallbackBox>.fromOpaque($0).takeUnretainedValue() }
    }
}

private func deviceID(_ bytes: UnsafePointer<UInt8>) -> DeviceID {
    DeviceID(validated: Array(UnsafeBufferPointer(start: bytes, count: Int(OSDROP_DEVICE_ID_SIZE))))
}
