import AppKit
import CoreBridge
import Foundation
import Observation

/// What the group feature needs. The app passes the real services; tests pass fakes.
struct GroupServices: Sendable {
    let keyStore: any KeyStoring
    let settings: any SettingsStoring
    let discovery: any PeerDiscovering
    let localNetwork: any LocalNetworkChecking
    let makeMesh: @Sendable (Mesh.Configuration) throws(CoreError) -> any MeshDriving
    let deviceName: String
    let clipboard: ClipboardServices
    let input: InputServices
    let notifier: any TransferNotifying
}

/// What the shared clipboard needs, started with the mesh.
struct ClipboardServices: Sendable {
    let makePasteboard: @MainActor @Sendable () -> any PasteboardAccess
    let staging: ClipboardStaging
    let converter: any ImageConverting
}

/// A computer nearby that is not a member of this computer's group.
struct NearbyComputer: Identifiable, Equatable {
    let device: DiscoveredDevice
    /// It belongs to another group: pairing is refused until one of the two leaves its group.
    let inAnotherGroup: Bool
    /// It speaks another protocol version: one of the two needs an update.
    let needsUpdate: Bool

    var id: DeviceID { device.advertisement.deviceID }
    var name: String { device.advertisement.name }
    var platform: PeerPlatform { device.advertisement.platform }
}

enum PairingFailure: Equatable {
    case rejected
    case busy
    case groupFull
    case inAnotherGroup
    /// It refuses this computer: blocked there.
    case refused
    case timedOut
    case unreachable

    init(_ error: CoreError) {
        switch error {
        case .pairingRejected: self = .rejected
        case .notTrusted: self = .refused
        case .peerBusy: self = .busy
        case .groupFull: self = .groupFull
        case .inAnotherGroup: self = .inAnotherGroup
        case .timedOut: self = .timedOut
        default: self = .unreachable
        }
    }
}

/// A pairing in progress, as the pairing window shows it.
enum PairingState: Equatable {
    /// This computer is reaching the other one.
    case dialing(name: String)
    /// Both computers show `code`; the user here decides.
    case confirm(connection: UInt64, code: UInt32, name: String)
    /// Accepted here; the other computer's user has not answered yet.
    case waiting(code: UInt32, name: String)
    case failed(name: String, reason: PairingFailure)
}

enum GroupStartFailure: Equatable {
    /// The Keychain refused or holds a damaged item.
    case keyStore
    /// The stored group was written by a newer Osdrop; it is kept, not overwritten.
    case newerVersion
    case mesh
}

/// The group on this Mac: starts the mesh from the stored identity and state, connects Bonjour to
/// it, runs pairings and keeps the member list for the menu and the developer tools.
@MainActor
@Observable
final class GroupController {
    private(set) var startFailure: GroupStartFailure?
    private(set) var deviceID: DeviceID?
    private(set) var groupID: GroupID?
    private(set) var listenPort: UInt16 = 0
    private(set) var members: [GroupMember] = []
    /// Computers no member pairs with until one unblocks them.
    private(set) var blocked: [BlockedDevice] = []
    private(set) var nearby: [NearbyComputer] = []
    private(set) var pairing: PairingState?
    private(set) var localNetwork: LocalNetworkAccess = .unknown
    private(set) var statistics: [DeviceID: SessionStatistics] = [:]
    private(set) var unresponsive: Set<DeviceID> = []
    /// A member removed this computer from its group.
    private(set) var wasRemoved = false
    private(set) var clipboard: ClipboardController?
    /// Transfers of dragged files, for the menu.
    private(set) var transfers: TransfersController?
    private(set) var input: InputController?
    /// Every member's monitors and placement, for the arrangement editor.
    private(set) var arrangement: [LayoutComputer] = []

    var isRunning: Bool { mesh != nil }
    /// The name other computers see: the one chosen in Settings, or the Mac's own.
    /// A stored name is cleaned again, so one saved by an older version cannot stop the mesh.
    var deviceName: String { settings.deviceName.map(DeviceName.clean) ?? services.deviceName }

    private let services: GroupServices
    private let screens: any ScreenDescribing
    private var mesh: (any MeshDriving)?
    private var advertisement: (any Advertising)?
    private var discovered: [String: DiscoveredDevice] = [:]
    private var pairingConnection: UInt64?
    private var pairingPeer: DeviceID?
    /// A group event arrived; the group read at start is older and must not overwrite it.
    private var groupFromEvent = false
    private var tasks: [Task<Void, Never>] = []
    private var browsing: Task<Void, Never>?

    let settings: AppSettings

    init(services: GroupServices, screens: any ScreenDescribing, settings: AppSettings) {
        self.services = services
        self.screens = screens
        self.settings = settings
    }

    // MARK: - Lifecycle

    func start() {
        guard mesh == nil, startFailure == nil else { return }
        let configuration: Mesh.Configuration
        do throws(KeyStoreError) {
            configuration = Mesh.Configuration(
                identity: try services.keyStore.identity(), name: deviceName,
                groupState: try services.keyStore.groupState(), layoutState: services.settings.layoutState(),
                listenPort: services.settings.listenPort())
        } catch {
            Log.group.error("Key store failed: \(String(describing: error), privacy: .public)")
            startFailure = .keyStore
            return
        }
        guard let mesh = makeMesh(configuration) else { return }
        self.mesh = mesh
        startClipboard(mesh)
        startTransfers(mesh)
        startInput(mesh)
        deviceID = mesh.deviceID
        listenPort = mesh.listenPort
        services.settings.saveListenPort(mesh.listenPort)
        tasks.append(
            Task { [weak self, events = mesh.events] in
                for await event in events {
                    self?.handle(event)
                }
            })
        tasks.append(
            Task { [weak self, updates = services.localNetwork.accessUpdates()] in
                for await access in updates {
                    self?.localNetwork = access
                }
            })
        browse()
        screens.onChange = { [weak self] in self?.reportScreens() }
        reportScreens()
        tasks.append(
            Task { [weak self] in
                let group = await Self.groupID(of: mesh)
                guard let self else { return }
                if !groupFromEvent {
                    groupID = group
                }
                advertise()
                await refreshMembers()
                await refreshArrangement()
            })
    }

    /// Ends every session before the Mac sleeps or Osdrop quits.
    func closeAll(sleeping: Bool) {
        mesh?.closeAll(sleeping: sleeping)
    }

    /// After waking: Bonjour is browsed again, so the mesh rediscovers and redials its members.
    func resumeAfterSleep() {
        guard mesh != nil else { return }
        discovered.removeAll()
        browse()
    }

    func stop() {
        clipboard?.stop()
        input?.stop()
        tasks.forEach { $0.cancel() }
        tasks.removeAll()
        browsing?.cancel()
        browsing = nil
        advertisement?.cancel()
        advertisement = nil
        screens.onChange = nil
    }

    // MARK: - Commands

    func pair(with computer: NearbyComputer) {
        guard let mesh, pairing == nil || isFailed(pairing) else { return }
        pairing = .dialing(name: computer.name)
        pairingPeer = computer.id
        pairingConnection = nil
        let device = computer.device
        Task { [weak self] in
            do throws(CoreError) {
                let connection = try await mesh.pair(host: device.host, port: device.port)
                if self?.pairingConnection == nil {
                    self?.pairingConnection = connection
                }
            } catch {
                self?.pairing = .failed(name: computer.name, reason: .unreachable)
            }
        }
    }

    /// The user's answer to the code both computers show.
    func answerPairing(accepted: Bool) {
        guard let mesh, case .confirm(let connection, let code, let name) = pairing else { return }
        pairing = accepted ? .waiting(code: code, name: name) : nil
        Task { [weak self] in
            do throws(CoreError) {
                try await mesh.confirmPairing(connection, accepted: accepted)
            } catch {
                Log.group.error("Answering the pairing failed: \(String(describing: error), privacy: .public)")
                if accepted {
                    self?.pairing = .failed(name: name, reason: .unreachable)
                }
            }
        }
    }

    /// Closes the pairing window; a code still on screen is rejected.
    func dismissPairing() {
        if case .confirm = pairing {
            answerPairing(accepted: false)
        }
        pairing = nil
        pairingPeer = nil
        pairingConnection = nil
    }

    func revoke(_ member: GroupMember) {
        guard let mesh, !member.isSelf else { return }
        Task { [weak self] in
            do throws(CoreError) {
                try await mesh.revoke(member.deviceID)
            } catch {
                Log.group.error("Removing a member failed: \(String(describing: error), privacy: .public)")
            }
            await self?.refreshMembers()
        }
    }

    /// Removes `member` and blocks it everywhere in the group.
    func block(_ member: GroupMember) {
        guard !member.isSelf else { return }
        block(member.deviceID)
    }

    /// Refuses the computer whose code is shown and blocks it everywhere in the group.
    func blockPairing() {
        guard case .confirm = pairing, let device = pairingPeer else { return }
        pairing = nil
        pairingPeer = nil
        pairingConnection = nil
        block(device)
    }

    func unblock(_ device: BlockedDevice) {
        guard let mesh else { return }
        Task { [weak self] in
            do throws(CoreError) {
                try await mesh.unblock(device.deviceID)
            } catch {
                Log.group.error("Unblocking failed: \(String(describing: error), privacy: .public)")
            }
            await self?.refreshMembers()
        }
    }

    private func block(_ device: DeviceID) {
        guard let mesh else { return }
        Task { [weak self] in
            do throws(CoreError) {
                try await mesh.block(device)
            } catch {
                Log.group.error("Blocking failed: \(String(describing: error), privacy: .public)")
            }
            await self?.refreshMembers()
        }
    }

    func leaveGroup() {
        guard let mesh else { return }
        Task { [weak self] in
            do throws(CoreError) {
                try await mesh.leaveGroup()
            } catch {
                Log.group.error("Leaving the group failed: \(String(describing: error), privacy: .public)")
            }
            await self?.refreshMembers()
        }
    }

    /// What `member` may do on this computer; the other computer hears of it at once.
    func setPermissions(_ permissions: MemberPermissions, for member: GroupMember) {
        guard let mesh, !member.isSelf else { return }
        Task { [weak self] in
            do throws(CoreError) {
                try await mesh.setPermissions(permissions, for: member.deviceID)
            } catch {
                Log.group.error("Changing permissions failed: \(String(describing: error), privacy: .public)")
            }
            await self?.refreshMembers()
        }
    }

    /// A new arrangement from the editor; members have it within a second. The problem when core
    /// refuses it (an overlap, a computer that touches no other).
    func setArrangement(_ placements: [Placement]) async -> CoreError? {
        guard let mesh else { return .internalFailure }
        do throws(CoreError) {
            try await mesh.setLayout(placements)
        } catch {
            await refreshArrangement()  // the editor goes back to the arrangement in force
            return error
        }
        await refreshArrangement()
        return nil
    }

    /// The name other computers see, cut to what the wire allows (64 bytes, no control
    /// characters); the mesh starts again with it, so sessions reconnect. Empty: the Mac's name.
    func rename(to name: String?) {
        let trimmed = name?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        let chosen = trimmed.isEmpty ? nil : DeviceName.clean(trimmed)
        guard chosen != settings.deviceName else { return }
        settings.deviceName = chosen
        restart()
    }

    private func restart() {
        stop()
        startFailure = nil  // the start that failed may have been the name's fault
        mesh = nil
        clipboard = nil
        transfers = nil
        input = nil
        members = []
        arrangement = []
        statistics = [:]
        unresponsive = []
        discovered = [:]
        start()
    }

    func dismissRemoval() {
        wasRemoved = false
    }

    func openLocalNetworkSettings() {
        guard let url = Tunables.Permissions.localNetworkSettings else { return }
        NSWorkspace.shared.open(url)
    }

    // MARK: - Clipboard

    private func startClipboard(_ mesh: any MeshDriving) {
        let clipboard = ClipboardController(
            mesh: mesh, pasteboard: services.clipboard.makePasteboard(), staging: services.clipboard.staging,
            converter: services.clipboard.converter,
            limits: { [settings] in
                ClipboardLimits(
                    text: Tunables.Clipboard.textLimit, image: Tunables.Clipboard.imageLimit,
                    files: settings.clipboardFilesLimit)
            },
            marksFiles: { [settings] in settings.marksReceivedFiles },
            isEnabled: { [settings] in settings.sharesClipboard },
            isShared: { [weak self] in
                guard let self, settings.sharesClipboard else { return false }
                return members.contains { !$0.isSelf && $0.isConnected }
            },
            nameOf: { [weak self] device in
                self?.members.first { $0.deviceID == device }?.name ?? String(localized: "clipboard.otherComputer")
            }, notifier: services.notifier)
        self.clipboard = clipboard
        clipboard.start()
    }

    // MARK: - Transfers

    private func startTransfers(_ mesh: any MeshDriving) {
        transfers = TransfersController(
            nameOf: { [weak self] device in self?.name(of: device) ?? String(localized: "clipboard.otherComputer") },
            notifier: services.notifier,
            cancelReceiving: { id in Task { await mesh.cancelTransfer(id) } },
            withdrawDrag: { dragID in
                Task {
                    do throws(CoreError) {
                        try await mesh.withdraw(.drag, objectID: UInt64(dragID))
                    } catch {
                        Log.drag.error("Withdrawing a drag failed: \(String(describing: error), privacy: .public)")
                    }
                }
            })
    }

    // MARK: - Input control

    private func startInput(_ mesh: any MeshDriving) {
        let input = InputController(
            mesh: mesh, services: services.input,
            makeDrags: { [services, transfers] injector in services.input.makeDrags(mesh, injector, transfers) }
        ) { [weak self] device in
            self?.name(of: device) ?? String(localized: "clipboard.otherComputer")
        }
        self.input = input
        input.start()
    }

    /// A member's name for notices; "the other computer" when unknown.
    func name(of device: DeviceID) -> String {
        members.first { $0.deviceID == device }?.name ?? String(localized: "clipboard.otherComputer")
    }

    // MARK: - Mesh events

    private func handle(_ event: MeshEvent) {
        switch event {
        case .groupChanged(let state, let group):
            groupFromEvent = true
            do throws(KeyStoreError) {
                try services.keyStore.saveGroupState(state)
            } catch {
                Log.group.error("Storing the group failed: \(String(describing: error), privacy: .public)")
            }
            if group != groupID {
                groupID = group
                advertise()
            }
            refreshMembersLater()
        case .leftGroup(let reason):
            wasRemoved = reason == .notTrusted
            refreshMembersLater()
        case .layoutChanged(let state):
            services.settings.saveLayoutState(state)
            refreshArrangementLater()
        case .pairingCode(let connection, let code, let peer):
            pairingConnection = connection
            pairingPeer = peer.deviceID
            pairing = .confirm(connection: connection, code: code, name: peer.name)
        case .pairingFailed(let connection, let reason):
            guard connection == pairingConnection, let name = pairingName else { return }
            pairing = .failed(name: name, reason: PairingFailure(reason))
            pairingConnection = nil
        case .peerConnected(let peer, _):
            unresponsive.remove(peer.deviceID)
            if peer.deviceID == pairingPeer, !isFailed(pairing) {
                pairing = nil
                pairingPeer = nil
                pairingConnection = nil
            }
            refreshMembersLater()
        case .peerDisconnected(let device, _):
            statistics[device] = nil
            unresponsive.remove(device)
            refreshMembersLater()
        case .peerResponsive(let device, let responsive):
            if responsive {
                unresponsive.remove(device)
            } else {
                unresponsive.insert(device)
            }
        case .statistics(let device, let stats):
            statistics[device] = stats
        case .log(let level, let message):
            log(level, message)
        case .clipboardChanged, .contentReceived:
            clipboard?.handle(event)
        case .transferFinished:
            clipboard?.handle(event)
            transfers?.handle(event)
        case .transferProgress:
            clipboard?.handle(event)
            transfers?.handle(event)
        case .sendProgress:
            transfers?.handle(event)
        case .offerReady:
            break  // core measures the drags it offers itself
        }
    }

    private var pairingName: String? {
        switch pairing {
        case .dialing(let name), .confirm(_, _, let name), .waiting(_, let name), .failed(let name, _): name
        case nil: nil
        }
    }

    private func isFailed(_ state: PairingState?) -> Bool {
        if case .failed = state { return true }
        return false
    }

    private func log(_ level: MeshLogLevel, _ message: String) {
        switch level {
        case .debug: Log.mesh.debug("\(message, privacy: .public)")
        case .info: Log.mesh.info("\(message, privacy: .public)")
        case .warning: Log.mesh.warning("\(message, privacy: .public)")
        case .error: Log.mesh.error("\(message, privacy: .public)")
        }
    }

    // MARK: - Discovery

    private func browse() {
        browsing?.cancel()
        browsing = Task { [weak self, stream = services.discovery.browse()] in
            for await event in stream {
                await self?.handle(event)
            }
        }
    }

    private func handle(_ event: DiscoveryEvent) async {
        guard let mesh else { return }
        switch event {
        case .found(let device):
            let advertisement = device.advertisement
            guard advertisement.deviceID != deviceID else { return }
            discovered[device.label] = device
            updateNearby()
            guard advertisement.protocolVersion == Advertisement.protocolVersion else { return }
            do throws(CoreError) {
                try await mesh.peerDiscovered(
                    advertisement.deviceID, group: advertisement.groupID, host: device.host, port: device.port)
            } catch {
                Log.discovery.error("Passing a discovery on failed: \(String(describing: error), privacy: .public)")
            }
        case .lost(let label):
            guard let device = discovered.removeValue(forKey: label) else { return }
            updateNearby()
            do throws(CoreError) {
                try await mesh.peerLost(device.advertisement.deviceID)
            } catch {
                Log.discovery.error("Passing a loss on failed: \(String(describing: error), privacy: .public)")
            }
        case .failed(let error):
            if error == .localNetworkDenied {
                localNetwork = .denied
            }
            Log.discovery.error("Browsing failed: \(String(describing: error), privacy: .public)")
        }
    }

    private func advertise() {
        advertisement?.cancel()
        advertisement = nil
        guard let deviceID else { return }
        do throws(DiscoveryError) {
            advertisement = try services.discovery.advertise(
                Advertisement(deviceID: deviceID, groupID: groupID, name: deviceName), port: listenPort)
        } catch {
            Log.discovery.error("Advertising failed: \(String(describing: error), privacy: .public)")
        }
    }

    private func updateNearby() {
        let hidden = Set(members.map(\.deviceID)).union(blocked.map(\.deviceID))
        nearby = discovered.values
            .filter { !hidden.contains($0.advertisement.deviceID) }
            .map { device in
                let other = device.advertisement.groupID
                return NearbyComputer(
                    device: device, inAnotherGroup: other != nil && groupID != nil && other != groupID,
                    needsUpdate: device.advertisement.protocolVersion != Advertisement.protocolVersion)
            }
            .sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }

    // MARK: - State

    private func refreshArrangementLater() {
        Task { [weak self] in await self?.refreshArrangement() }
    }

    private func refreshArrangement() async {
        guard let mesh else { return }
        do throws(CoreError) {
            arrangement = try await mesh.layout()
        } catch {
            Log.group.error("Reading the arrangement failed: \(String(describing: error), privacy: .public)")
        }
    }

    private func refreshMembersLater() {
        Task { [weak self] in await self?.refreshMembers() }
    }

    private func refreshMembers() async {
        guard let mesh else { return }
        do throws(CoreError) {
            members = try await mesh.members()
            blocked = try await mesh.blocked()
            input?.setActive(members.contains { !$0.isSelf && $0.isConnected })
        } catch {
            Log.group.error("Listing members failed: \(String(describing: error), privacy: .public)")
        }
        updateNearby()
    }

    private func reportScreens() {
        guard let mesh else { return }
        let monitors = screens.screens().map(\.monitor)
        guard !monitors.isEmpty else { return }
        Task { [mesh, monitors] in
            do throws(CoreError) {
                try await mesh.setScreens(monitors)
            } catch {
                Log.screens.error("Reporting screens failed: \(String(describing: error), privacy: .public)")
            }
        }
    }

    /// The mesh, or `nil` with `startFailure` set. A damaged arrangement is dropped and rebuilt; a
    /// group state that cannot be read stops the start, so trust is never lost silently.
    private func makeMesh(_ configuration: Mesh.Configuration) -> (any MeshDriving)? {
        do throws(CoreError) {
            return try services.makeMesh(configuration)
        } catch {
            if error == .unsupportedVersion {
                startFailure = .newerVersion
                return nil
            }
            guard configuration.layoutState != nil else {
                Log.group.error("Starting the mesh failed: \(String(describing: error), privacy: .public)")
                startFailure = .mesh
                return nil
            }
        }
        var withoutLayout = configuration
        withoutLayout.layoutState = nil
        do throws(CoreError) {
            let mesh = try services.makeMesh(withoutLayout)
            Log.group.warning("The stored arrangement could not be read; it starts over")
            return mesh
        } catch {
            Log.group.error("Starting the mesh failed: \(String(describing: error), privacy: .public)")
            startFailure = error == .unsupportedVersion ? .newerVersion : .mesh
            return nil
        }
    }

    private static func groupID(of mesh: any MeshDriving) async -> GroupID? {
        do throws(CoreError) {
            return try await mesh.groupID()
        } catch {
            Log.group.error("Reading the group failed: \(String(describing: error), privacy: .public)")
            return nil
        }
    }
}
