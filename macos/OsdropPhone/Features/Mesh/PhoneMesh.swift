import CoreBridge
import Foundation
import Observation
import UniformTypeIdentifiers

/// A pairing code to compare, and the computer it pairs with.
struct PairingPrompt: Equatable, Identifiable {
    let connection: UInt64
    let code: UInt32
    let name: String

    var id: UInt64 { connection }
}

/// This phone's member of the group while Osdrop is in the foreground (DEVPLAN Phase 22; iOS
/// suspends the sockets of an app in the background): `start()` when the app comes to the front,
/// `stop()` when it leaves. It pairs with computers, takes the files they send and sends files to
/// them, also the ones the share extension left in the App Group.
@MainActor
@Observable
final class PhoneMesh {
    private(set) var isRunning = false
    /// Why the mesh could not start; nil while it runs or before it was started.
    private(set) var startFailure: CoreError?
    private(set) var members: [GroupMember] = []
    /// Computers on the network that are not in this phone's group.
    private(set) var nearby: [DiscoveredDevice] = []
    var pairing: PairingPrompt?
    /// Why the last pairing failed; nil when none did.
    var pairingFailure: CoreError?
    let transfers = PhoneTransfers()

    @ObservationIgnored private let keyStore: any KeyStoring
    @ObservationIgnored private let discovery: any PeerDiscovering
    @ObservationIgnored private let handoff: Handoff?
    @ObservationIgnored private let name: String
    @ObservationIgnored private let receivedFolder: URL
    @ObservationIgnored private var mesh: Mesh?
    @ObservationIgnored private var inbox: PhoneInbox?
    @ObservationIgnored private var advertising: (any Advertising)?
    @ObservationIgnored private var tasks: [Task<Void, Never>] = []
    @ObservationIgnored private var discovered: [String: DiscoveredDevice] = [:]  // by label
    /// Staged copies of each send, removed once it ended.
    @ObservationIgnored private var staged: [UInt64: [URL]] = [:]
    /// The share extension's sets on their way, by send.
    @ObservationIgnored private var handedOff: [UInt64: Handoff.PendingSend] = [:]
    @ObservationIgnored private var groupID: GroupID?

    init(
        keyStore: any KeyStoring, discovery: any PeerDiscovering, handoff: Handoff?, name: String,
        receivedFolder: URL
    ) {
        self.keyStore = keyStore
        self.discovery = discovery
        self.handoff = handoff
        self.name = name
        self.receivedFolder = receivedFolder
    }

    /// The computers of the group, connected or not; phones exchange files with computers only.
    var computers: [GroupMember] {
        members.filter { !$0.isSelf && !$0.platform.isPhone }
    }

    var isInGroup: Bool { !computers.isEmpty }

    /// Files still on their way, either way: the app asks for time to finish them in the background.
    var hasRunningTransfers: Bool {
        transfers.rows.contains { $0.state == .running }
    }

    // MARK: - Lifecycle

    func start() {
        guard mesh == nil else { return }
        let mesh: Mesh
        do {
            let identity = try keyStore.identity()
            let state = try keyStore.groupState()
            mesh = try Mesh(
                .init(
                    identity: identity, name: name, groupState: state, features: .files,
                    maxTransferStreams: Tunables.Mesh.transferStreams))
        } catch let error as CoreError {
            Log.mesh.error("The mesh did not start: \(String(describing: error), privacy: .public)")
            startFailure = error
            return
        } catch {
            Log.mesh.error("The identity could not be read: \(String(describing: error), privacy: .public)")
            startFailure = .internalFailure
            return
        }
        self.mesh = mesh
        startFailure = nil
        isRunning = true
        groupID = try? mesh.groupID()
        inbox = PhoneInbox(
            receive: { item, device, folder throws(CoreError) in
                try mesh.receive(item, from: device, into: folder)
            },
            answer: { sendID, device, outcome in
                do throws(CoreError) {
                    try mesh.answerSend(sendID, from: device, outcome)
                } catch {
                    Log.files.error("Answering a send failed: \(String(describing: error), privacy: .public)")
                }
            }, transfers: transfers, folder: receivedFolder)
        let events = mesh.events
        tasks.append(
            Task { [weak self] in
                for await event in events {
                    self?.handle(event)
                }
            })
        let browsing = discovery.browse()
        tasks.append(
            Task { [weak self] in
                for await event in browsing {
                    self?.discovered(event)
                }
            })
        advertise()
        refreshMembers()
        handoff?.removeUnsealed(olderThan: Tunables.Handoff.unsealedAge)
        sendHandedOff()
    }

    /// Ends every session (the computers see this phone leave) and stops listening.
    func stop() {
        guard let mesh else { return }
        tasks.forEach { $0.cancel() }
        tasks = []
        advertising?.cancel()
        advertising = nil
        mesh.closeAll(sleeping: false)
        self.mesh = nil
        inbox?.reset()
        inbox = nil
        discovered = [:]
        nearby = []
        pairing = nil
        isRunning = false
        handedOff = [:]  // sent again next time
        for (sendID, folders) in staged {
            folders.forEach { try? FileManager.default.removeItem(at: $0) }
            transfers.sendFinished(sendID, .failed)
        }
        staged = [:]
    }

    // MARK: - Pairing and the group

    func pair(_ device: DiscoveredDevice) {
        guard let mesh else { return }
        pairingFailure = nil
        do throws(CoreError) {
            _ = try mesh.pair(host: device.host, port: device.port)
        } catch {
            pairingFailure = error
        }
    }

    func answerPairing(accepted: Bool) {
        guard let prompt = pairing else { return }
        pairing = nil
        try? mesh?.confirmPairing(prompt.connection, accepted: accepted)
    }

    func leaveGroup() {
        try? mesh?.leaveGroup()
    }

    // MARK: - Sending

    /// Sends staged copies (their folders are removed once the send ended) to a connected
    /// computer. False when it could not start.
    @discardableResult
    func send(_ files: [StagedFile], to computer: DeviceID) -> Bool {
        let folders = files.map(\.folder)
        guard let sendID = startSend(files.map(\.url), to: computer) else {
            folders.forEach { try? FileManager.default.removeItem(at: $0) }
            return false
        }
        staged[sendID] = folders
        return true
    }

    func cancel(_ row: PhoneTransfers.Row) {
        guard let mesh, row.state == .running else { return }
        transfers.markCancelled(row.id)
        switch row.id {
        case .incoming(let id):
            mesh.cancelTransfer(id)
        case .outgoing(let sendID, _):
            try? mesh.withdraw(.sent, objectID: sendID)
        }
    }

    private func startSend(_ files: [URL], to computer: DeviceID) -> UInt64? {
        guard let mesh, !files.isEmpty else { return nil }
        let items = files.map { url in
            let type = (try? url.resourceValues(forKeys: [.contentTypeKey]))?.contentType ?? .data
            return DraggedFile(url: url, contentType: type.identifier)
        }
        do throws(CoreError) {
            let sendID = try mesh.send(items, to: computer)
            transfers.offered(sendID, to: computer, names: files.map(\.lastPathComponent))
            return sendID
        } catch {
            Log.files.error("Sending files failed: \(String(describing: error), privacy: .public)")
            return nil
        }
    }

    /// Sends what the share extension left, to the computers that are connected now.
    private func sendHandedOff() {
        guard let handoff else { return }
        let connected = Set(computers.filter(\.isConnected).map(\.deviceID.hex))
        let onTheirWay = Set(handedOff.values.map(\.folder))
        for pending in handoff.pendingSends() where !onTheirWay.contains(pending.folder) {
            guard connected.contains(pending.computer), let device = DeviceID(hex: pending.computer) else { continue }
            if let sendID = startSend(pending.files, to: device) {
                handedOff[sendID] = pending
            }
        }
    }

    /// Lets the share extension offer the group's computers.
    private func shareComputers() {
        let list = computers.map {
            Handoff.Computer(id: $0.deviceID.hex, name: $0.name, isWindows: $0.platform == .windows)
        }
        do {
            try handoff?.saveComputers(list)
        } catch {
            Log.files.error("The computers could not be shared: \(error.localizedDescription, privacy: .public)")
        }
    }

    // MARK: - Mesh events

    private func handle(_ event: MeshEvent) {
        switch event {
        case .groupChanged(let state, let groupID):
            do {
                try keyStore.saveGroupState(state)
            } catch {
                Log.mesh.error("The group could not be stored: \(String(describing: error), privacy: .public)")
            }
            self.groupID = groupID
            advertise()
            refreshMembers()
        case .leftGroup:
            refreshMembers()
        case .pairingCode(let connection, let code, let peer):
            pairing = PairingPrompt(connection: connection, code: code, name: peer.name)
        case .pairingFailed(let connection, let reason):
            if pairing?.connection == connection {
                pairing = nil
            }
            pairingFailure = reason
        case .peerConnected, .peerDisconnected:
            refreshMembers()
        case .transferProgress(let progress):
            transfers.progress(progress)
        case .transferFinished(let id, let result):
            transfers.finished(id, result)
            inbox?.finished(id, succeeded: (try? result.get()) != nil)
        case .sendArrived(let device, let sendID, let items):
            inbox?.arrived(from: device, sendID: sendID, items: items)
        case .sendProgress(let progress):
            transfers.sent(progress)
        case .sendFinished(_, let sendID, let outcome):
            transfers.sendFinished(sendID, outcome)
            staged.removeValue(forKey: sendID)?.forEach { try? FileManager.default.removeItem(at: $0) }
            if let pending = handedOff.removeValue(forKey: sendID) {
                handoff?.remove(pending)
            }
        case .log(let level, let message):
            if level == .warning || level == .error {
                Log.mesh.warning("\(message, privacy: .public)")
            } else {
                Log.mesh.info("\(message, privacy: .public)")
            }
        default:
            break  // control, drags and the clipboard are the computers'
        }
    }

    private func refreshMembers() {
        guard let mesh else { return }
        members = (try? mesh.members()) ?? []
        refreshNearby()
        shareComputers()
        sendHandedOff()
    }

    // MARK: - Discovery

    private func advertise() {
        guard let mesh else { return }
        advertising?.cancel()
        do throws(DiscoveryError) {
            advertising = try discovery.advertise(
                Advertisement(deviceID: mesh.deviceID, groupID: groupID, name: name), port: mesh.listenPort)
        } catch {
            Log.discovery.error("Advertising failed: \(String(describing: error), privacy: .public)")
        }
    }

    private func discovered(_ event: DiscoveryEvent) {
        guard let mesh else { return }
        switch event {
        case .found(let device):
            guard !device.advertisement.platform.isPhone, device.advertisement.deviceID != mesh.deviceID else { return }
            discovered[device.label] = device
            try? mesh.peerDiscovered(
                device.advertisement.deviceID, group: device.advertisement.groupID, host: device.host,
                port: device.port)
        case .lost(let label):
            if let device = discovered.removeValue(forKey: label) {
                try? mesh.peerLost(device.advertisement.deviceID)
            }
        case .failed(let error):
            Log.discovery.error("Browsing failed: \(String(describing: error), privacy: .public)")
        }
        refreshNearby()
    }

    private func refreshNearby() {
        let inGroup = Set(members.map(\.deviceID))
        var seen: Set<DeviceID> = []
        nearby = discovered.values
            .filter { !inGroup.contains($0.advertisement.deviceID) && seen.insert($0.advertisement.deviceID).inserted }
            .sorted { $0.advertisement.name < $1.advertisement.name }
    }
}
