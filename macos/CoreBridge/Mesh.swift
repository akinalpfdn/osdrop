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
    /// Input control, apart from `events` so injection never waits behind other events.
    public let control: AsyncStream<ControlEvent>
    /// Transfers by their ID, followed without the main thread.
    public let transfers: TransferWatch
    public let deviceID: DeviceID
    public let listenPort: UInt16

    private let handle: MeshHandle
    /// Holds the callbacks' context; must outlive the core mesh.
    private let box: MeshCallbackBox

    /// Starts listening and core's I/O thread. Throws `.unsupportedVersion` for a stored state
    /// written by a newer Osdrop (keep it; do not overwrite it).
    public init(_ configuration: Configuration) throws(CoreError) {
        let (events, continuation) = AsyncStream.makeStream(of: MeshEvent.self)
        let (control, controlContinuation) = AsyncStream.makeStream(of: ControlEvent.self)
        let transfers = TransferWatch()
        let box = MeshCallbackBox(continuation: continuation, control: controlContinuation, transfers: transfers)
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
            controlContinuation.finish()
            throw error
        }
        guard let created else {
            continuation.finish()
            controlContinuation.finish()
            throw .internalFailure
        }
        var port: UInt16 = 0
        _ = osdrop_mesh_listen_port(created, &port)
        self.events = events
        self.control = control
        self.transfers = transfers
        self.box = box
        self.handle = MeshHandle(created)
        self.deviceID = configuration.identity.id
        self.listenPort = port
    }

    deinit {
        handle.destroy()
        box.continuation.finish()
        box.control.finish()
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

    // MARK: Transfers

    /// Offers local files and folders as one item to the members in `audience`; scanning starts at
    /// once and `.offerReady` follows. Replaces an earlier offer of the same item.
    public func offer(_ item: TransferItem, files: [URL], to audience: [DeviceID]) throws(CoreError) {
        var raw = item.raw
        let ids = audience.flatMap(\.bytes)
        try check(
            withCStrings(files.map { $0.path(percentEncoded: false) }) { paths in
                ids.withUnsafeBufferPointer { devices in
                    handle.withPointer {
                        osdrop_mesh_offer($0, &raw, paths, paths.count, devices.baseAddress, audience.count)
                    }
                }
            })
    }

    /// Withdraws every item of an object; members fetching them get `.cancelled`.
    public func withdraw(_ kind: TransferItem.Kind, objectID: UInt64) throws(CoreError) {
        try check(handle.withPointer { osdrop_mesh_withdraw($0, kind.raw, objectID) })
    }

    /// Fetches an item a member offered into `folder`; the transfer's ID. It ends with `.tooLarge`
    /// when its files add up to more than `maxBytes`. `.notPermitted` when the member may not send
    /// this computer files.
    public func receive(
        _ item: TransferItem, from device: DeviceID, into folder: URL, maxBytes: UInt64 = .max
    ) throws(CoreError) -> UInt64 {
        var raw = item.raw
        var id: UInt64 = 0
        try check(
            folder.path(percentEncoded: false).withCString { path in
                handle.withPointer { osdrop_mesh_receive($0, device.bytes, &raw, path, maxBytes, &id) }
            })
        return id
    }

    /// Fetches one format (not `.files`) of another member's copy into memory; the result arrives
    /// as `.contentReceived`, `.tooLarge` beyond `maxBytes`.
    public func fetchContent(
        _ item: TransferItem, from device: DeviceID, maxBytes: UInt64
    ) throws(CoreError)
        -> UInt64
    {
        var raw = item.raw
        var id: UInt64 = 0
        try check(handle.withPointer { osdrop_mesh_fetch_content($0, device.bytes, &raw, maxBytes, &id) })
        return id
    }

    // MARK: Input control

    /// Physical input on this Mac, captured `age` microseconds ago: all of it, wherever the cursor
    /// is. Returns at once, from any thread.
    public func input(_ input: ControlInput, ageMicroseconds age: UInt64) throws(CoreError) {
        var raw = input.raw
        try check(handle.withPointer { osdrop_mesh_input($0, &raw, age) })
    }

    // MARK: Drags

    /// A drag began on this Mac with these files and folders, or the files of a wanted drag of
    /// promised files arrived: they go wherever the cursor carries the drag. An empty list when it
    /// carries nothing that can travel. The drag's ID, 0 without files.
    @discardableResult
    public func dragStarted(_ files: [DraggedFile]) throws(CoreError) -> UInt32 {
        var dragID: UInt32 = 0
        try check(
            withCStrings(files.map { $0.url.path(percentEncoded: false) }) { paths in
                withCStrings(files.map(\.contentType)) { types in
                    let items = zip(paths, types).map { osdrop_dragged_item(path: $0, content_type: $1) }
                    return items.withUnsafeBufferPointer { buffer in
                        handle.withPointer { osdrop_mesh_drag_started($0, buffer.baseAddress, buffer.count, &dragID) }
                    }
                }
            })
        return dragID
    }

    /// A drag began on this Mac with promised files, which exist only once it is dropped (Photos):
    /// `.dragWanted` asks for them when another computer wants the drag.
    public func dragPromised() throws(CoreError) {
        try check(handle.withPointer { osdrop_mesh_drag_promised($0) })
    }

    /// This Mac's drag ended, however it ended.
    public func dragStopped() throws(CoreError) {
        try check(handle.withPointer { osdrop_mesh_drag_stopped($0) })
    }

    /// The drag started for `.dragArrived` ended.
    public func dropFinished(_ dragID: UInt32, _ outcome: DropOutcome) throws(CoreError) {
        try check(handle.withPointer { osdrop_mesh_drop_finished($0, dragID, outcome.raw) })
    }

    /// Brings the cursor home: the user's hotkey, or `inputLost` when the capture ended by itself.
    public func returnHome(inputLost: Bool) throws(CoreError) {
        try check(handle.withPointer { osdrop_mesh_return_home($0, inputLost ? 1 : 0) })
    }

    /// "Lock input here": the cursor stays on the computer it is on.
    public func lockInput(_ locked: Bool) throws(CoreError) {
        try check(handle.withPointer { osdrop_mesh_lock_input($0, locked ? 1 : 0) })
    }

    /// Whether other computers may control this one now.
    public func setUnavailable(_ reason: ControlUnavailability?) throws(CoreError) {
        try check(handle.withPointer { osdrop_mesh_set_unavailable($0, reason?.code ?? 0) })
    }

    public func setInputSettings(_ settings: InputSettings) throws(CoreError) {
        var raw = settings.raw
        try check(handle.withPointer { osdrop_mesh_set_input_settings($0, &raw) })
    }

    /// The pointer speed on `device`, in thousandths of this Mac's own: 250 to 4000.
    public func setPointerSpeed(_ permille: UInt32, for device: DeviceID) throws(CoreError) {
        try check(handle.withPointer { osdrop_mesh_set_pointer_speed($0, device.bytes, permille) })
    }

    /// Caps Lock on this Mac, whenever it changes while the cursor is home.
    public func setCapsLock(_ on: Bool) throws(CoreError) {
        try check(handle.withPointer { osdrop_mesh_set_caps_lock($0, on ? 1 : 0) })
    }

    /// While controlled: where this Mac's cursor really is, when something else moved it.
    public func cursorMoved(to position: CoordPoint) throws(CoreError) {
        try check(handle.withPointer { osdrop_mesh_cursor_moved($0, position.x, position.y) })
    }

    /// A copy on this computer: the contents of its text, RTF, HTML and PNG formats, and copied
    /// files. It is announced to the members sharing clipboards with this computer; the new clip's
    /// ID.
    public func clipboardCopied(_ contents: [ClipboardContent], files: [URL]) throws(CoreError) -> UInt64 {
        let buffers = contents.map { content in
            let buffer = UnsafeMutableBufferPointer<UInt8>.allocate(capacity: max(content.data.count, 1))
            _ = content.data.copyBytes(to: buffer)
            return buffer
        }
        defer { buffers.forEach { $0.deallocate() } }
        let raw = zip(contents, buffers).map { content, buffer in
            osdrop_clipboard_content(
                format: content.format.raw, data: UnsafePointer(buffer.baseAddress), size: content.data.count)
        }
        var id: UInt64 = 0
        try check(
            withCStrings(files.map { $0.path(percentEncoded: false) }) { paths in
                raw.withUnsafeBufferPointer { items in
                    handle.withPointer {
                        osdrop_mesh_clipboard_copied($0, items.baseAddress, items.count, paths, paths.count, &id)
                    }
                }
            })
        return id
    }

    /// Ends a transfer with `.cancelled`; what it created is removed.
    public func cancelTransfer(_ id: UInt64) {
        _ = handle.withPointer { osdrop_mesh_cancel_transfer($0, id) }
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
    let control: AsyncStream<ControlEvent>.Continuation
    let transfers: TransferWatch

    init(
        continuation: AsyncStream<MeshEvent>.Continuation, control: AsyncStream<ControlEvent>.Continuation,
        transfers: TransferWatch
    ) {
        self.continuation = continuation
        self.control = control
        self.transfers = transfers
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
        callbacks.offer_ready = { context, item, result, totalBytes, entryCount, skipped in
            guard let item = item?.pointee else { return }
            let outcome: Result<OfferSummary, CoreError> =
                if let error = CoreError(result: result) {
                    .failure(error)
                } else {
                    .success(OfferSummary(totalBytes: totalBytes, entryCount: entryCount, skipped: skipped))
                }
            MeshCallbackBox.from(context)?.continuation.yield(.offerReady(TransferItem(item), outcome))
        }
        callbacks.transfer_progress = { context, progress in
            guard let progress = progress?.pointee, let box = MeshCallbackBox.from(context) else { return }
            let update = TransferProgress(progress)
            box.transfers.progressed(update)
            box.continuation.yield(.transferProgress(update))
        }
        callbacks.transfer_finished = { context, id, result, items, count, skippedLinks in
            let outcome: Result<ReceivedItem, CoreError>
            if let error = CoreError(result: result) {
                outcome = .failure(error)
            } else {
                let paths = (0..<count).compactMap { index in items?[index].map { String(cString: $0) } }
                outcome = .success(
                    ReceivedItem(items: paths.map { URL(filePath: $0) }, skippedLinks: skippedLinks))
            }
            guard let box = MeshCallbackBox.from(context) else { return }
            box.transfers.finished(id, outcome)
            box.continuation.yield(.transferFinished(id: id, outcome))
        }
        callbacks.clipboard_changed = { context, device, clipID, formats, count in
            guard let device else { return }
            let announced = (0..<count).compactMap { index -> AnnouncedFormat? in
                guard let raw = formats?[index], let format = ClipboardFormat(raw.format) else { return nil }
                return AnnouncedFormat(format: format, byteCount: raw.byte_count)
            }
            MeshCallbackBox.from(context)?.continuation.yield(
                .clipboardChanged(deviceID(device), clipID: clipID, formats: announced))
        }
        callbacks.content_received = { context, id, result, data, size in
            let outcome: Result<Data, CoreError> =
                if let error = CoreError(result: result) {
                    .failure(error)
                } else {
                    .success(data.map { Data(bytes: $0, count: size) } ?? Data())
                }
            MeshCallbackBox.from(context)?.continuation.yield(.contentReceived(id: id, outcome))
        }
        callbacks.capture_began = { context, device, keys, count in
            guard let device else { return }
            let released = (0..<count).compactMap { index in keys.map { ControlInput($0[index]) } }
            MeshCallbackBox.from(context)?.control.yield(.captureBegan(deviceID(device), releaseHere: released))
        }
        callbacks.capture_ended = { context, device, x, y, reason, refused, capsLock in
            guard let device else { return }
            MeshCallbackBox.from(context)?.control.yield(
                .captureEnded(
                    deviceID(device), cursor: CoordPoint(x: x, y: y),
                    reason: CaptureEnd(reason: reason, refused: refused), capsLock: capsLock == 1))
        }
        callbacks.controlled = { context, controller in
            MeshCallbackBox.from(context)?.control.yield(.controlled(by: controller.map { deviceID($0) }))
        }
        callbacks.inject = { context, input in
            guard let input = input?.pointee else { return }
            MeshCallbackBox.from(context)?.control.yield(.inject(ControlInput(input)))
        }
        callbacks.input_latency = { context, controller, samples, median, p99 in
            guard let controller else { return }
            MeshCallbackBox.from(context)?.control.yield(
                .inputLatency(
                    deviceID(controller), samples: samples, medianMicroseconds: median, p99Microseconds: p99))
        }
        callbacks.drag_arrived = { context, source, dragID, x, y, items, count in
            guard let source else { return }
            let arriving = (0..<count).compactMap { index -> ArrivingItem? in
                guard let item = items?[index], let name = item.name, let type = item.content_type else { return nil }
                return ArrivingItem(
                    name: String(cString: name), isFolder: item.is_folder == 1, byteCount: item.byte_count,
                    contentType: String(cString: type))
            }
            MeshCallbackBox.from(context)?.control.yield(
                .dragArrived(source: deviceID(source), dragID: dragID, at: CoordPoint(x: x, y: y), items: arriving))
        }
        callbacks.drag_withdrawn = { context, dragID in
            MeshCallbackBox.from(context)?.control.yield(.dragWithdrawn(dragID: dragID))
        }
        callbacks.drag_ended = { context, dragID, outcome in
            MeshCallbackBox.from(context)?.control.yield(.dragEnded(dragID: dragID, DropOutcome(outcome)))
        }
        callbacks.drag_wanted = { context in
            MeshCallbackBox.from(context)?.control.yield(.dragWanted)
        }
        callbacks.send_progress = { context, device, item, sent, total, state in
            guard let device, let item = item?.pointee else { return }
            MeshCallbackBox.from(context)?.continuation.yield(
                .sendProgress(
                    SendProgress(
                        device: deviceID(device), item: TransferItem(item), bytesSent: sent, bytesTotal: total,
                        state: SendState(state))))
        }
        return callbacks
    }

    /// Core passes back the context it was given; Mesh keeps the box alive.
    static func from(_ context: UnsafeMutableRawPointer?) -> MeshCallbackBox? {
        context.map { Unmanaged<MeshCallbackBox>.fromOpaque($0).takeUnretainedValue() }
    }
}

/// Runs `body` with NUL-terminated copies of `strings`, freed afterwards.
private func withCStrings<Result>(
    _ strings: [String], _ body: ([UnsafePointer<CChar>?]) -> Result
) -> Result {
    let copies = strings.map { strdup($0) }
    defer { copies.forEach { free($0) } }
    return body(copies.map { $0.map { UnsafePointer($0) } })
}

private func deviceID(_ bytes: UnsafePointer<UInt8>) -> DeviceID {
    DeviceID(validated: Array(UnsafeBufferPointer(start: bytes, count: Int(OSDROP_DEVICE_ID_SIZE))))
}
