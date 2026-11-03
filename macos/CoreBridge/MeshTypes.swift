import OsdropCore

public enum PeerPlatform: Sendable, Equatable {
    case macOS
    case windows

    /// The platform this app runs on.
    public static var current: PeerPlatform { .macOS }

    init(_ raw: osdrop_platform) {
        self = raw == OSDROP_PLATFORM_WINDOWS ? .windows : .macOS
    }

    var raw: osdrop_platform {
        switch self {
        case .macOS: OSDROP_PLATFORM_MACOS
        case .windows: OSDROP_PLATFORM_WINDOWS
        }
    }
}

/// Capabilities feature bits (docs/PROTOCOL.md "Capabilities").
public struct MeshFeatures: OptionSet, Sendable, Hashable {
    public let rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }

    public static let takesControl = MeshFeatures(rawValue: OSDROP_FEATURE_TAKES_CONTROL)
    public static let dragSource = MeshFeatures(rawValue: OSDROP_FEATURE_DRAG_SOURCE)
    public static let dragTarget = MeshFeatures(rawValue: OSDROP_FEATURE_DRAG_TARGET)
    public static let clipboard = MeshFeatures(rawValue: OSDROP_FEATURE_CLIPBOARD)
    public static let files = MeshFeatures(rawValue: OSDROP_FEATURE_FILES)
    public static let all: MeshFeatures = [.takesControl, .dragSource, .dragTarget, .clipboard, .files]
}

/// A computer as its Hello and handshake describe it.
public struct MeshPeer: Sendable, Equatable {
    public let deviceID: DeviceID
    public let publicKey: [UInt8]
    public let platform: PeerPlatform
    public let protocolMinor: UInt8
    public let groupID: GroupID?
    public let name: String

    init(_ peer: osdrop_peer) {
        deviceID = DeviceID(field: peer.device_id)
        publicKey = CArray.bytes(of: peer.public_key)
        platform = PeerPlatform(peer.platform)
        protocolMinor = peer.protocol_minor
        groupID = peer.has_group == 1 ? GroupID(bytes: CArray.bytes(of: peer.group_id)) : nil
        name = CArray.string(of: peer.name)
    }
}

/// What this computer accepts from one member.
public struct MemberPermissions: Sendable, Equatable {
    public var control: Bool
    public var files: Bool
    public var clipboard: Bool

    public static let all = MemberPermissions(control: true, files: true, clipboard: true)

    public init(control: Bool, files: Bool, clipboard: Bool) {
        self.control = control
        self.files = files
        self.clipboard = clipboard
    }

    init(_ raw: osdrop_permissions) {
        self.init(control: raw.control == 1, files: raw.files == 1, clipboard: raw.clipboard == 1)
    }

    var raw: osdrop_permissions {
        osdrop_permissions(control: control ? 1 : 0, files: files ? 1 : 0, clipboard: clipboard ? 1 : 0)
    }
}

public struct GroupMember: Sendable, Equatable, Identifiable {
    public let deviceID: DeviceID
    public let publicKey: [UInt8]
    public let platform: PeerPlatform
    /// As it was when the computer joined.
    public let name: String
    public let permissions: MemberPermissions
    public let isSelf: Bool
    public let isConnected: Bool

    public var id: DeviceID { deviceID }

    init(_ member: osdrop_member) {
        deviceID = DeviceID(field: member.device_id)
        publicKey = CArray.bytes(of: member.public_key)
        platform = PeerPlatform(member.platform)
        name = CArray.string(of: member.name)
        permissions = MemberPermissions(member.permissions)
        isSelf = member.`self` == 1
        isConnected = member.connected == 1
    }
}

public struct SessionStatistics: Sendable, Equatable {
    public let roundTripMicroseconds: UInt32
    /// The peer's clock minus this computer's.
    public let clockOffsetMicroseconds: Int64
    public let datagramsRejected: UInt64
    public let staleMotionDropped: UInt64

    init(_ stats: osdrop_session_stats) {
        roundTripMicroseconds = stats.rtt_us
        clockOffsetMicroseconds = stats.clock_offset_us
        datagramsRejected = stats.datagrams_rejected
        staleMotionDropped = stats.stale_motion_dropped
    }
}

public enum MeshLogLevel: Sendable, Equatable {
    case debug
    case info
    case warning
    case error

    init(_ raw: osdrop_log_level) {
        switch raw {
        case OSDROP_LOG_DEBUG: self = .debug
        case OSDROP_LOG_INFO: self = .info
        case OSDROP_LOG_WARNING: self = .warning
        default: self = .error
        }
    }
}

/// What the mesh reports, in order, from core's I/O thread.
public enum MeshEvent: Sendable, Equatable {
    /// Store `state` in the secure store; advertise `groupID`.
    case groupChanged(state: [UInt8], groupID: GroupID?)
    /// `nil` when this computer left; `.notTrusted` when a member removed it.
    case leftGroup(reason: CoreError?)
    /// Store `state` with the settings.
    case layoutChanged(state: [UInt8])
    /// A first pairing: show `code` (six digits) and answer with `confirmPairing`.
    case pairingCode(connection: UInt64, code: UInt32, peer: MeshPeer)
    case pairingFailed(connection: UInt64, reason: CoreError)
    case peerConnected(MeshPeer, features: MeshFeatures)
    /// `nil` for an orderly end.
    case peerDisconnected(DeviceID, reason: CoreError?)
    /// False after 750 ms of silence: input to and from it stops.
    case peerResponsive(DeviceID, Bool)
    case statistics(DeviceID, SessionStatistics)
    case log(MeshLogLevel, String)
}

/// A rectangle in 1/16 of a unit (docs/PROTOCOL.md "Conventions").
public struct CoordRect: Sendable, Equatable {
    public var x: Int32
    public var y: Int32
    public var width: Int32
    public var height: Int32

    public init(x: Int32, y: Int32, width: Int32, height: Int32) {
        self.x = x
        self.y = y
        self.width = width
        self.height = height
    }

    init(_ raw: osdrop_rect) {
        self.init(x: raw.x, y: raw.y, width: raw.width, height: raw.height)
    }

    var raw: osdrop_rect { osdrop_rect(x: x, y: y, width: width, height: height) }
}

/// One monitor of a computer: its logical frame and its native rectangle (on macOS the same).
public struct ScreenMonitor: Sendable, Equatable {
    /// Stable identity on its computer, OSDROP_MONITOR_KEY_SIZE bytes.
    public let key: [UInt8]
    public let frame: CoordRect
    public let native: CoordRect
    public let pixelWidth: UInt16
    public let pixelHeight: UInt16
    public let isPrimary: Bool

    public init(
        key: [UInt8], frame: CoordRect, native: CoordRect, pixelWidth: UInt16, pixelHeight: UInt16,
        isPrimary: Bool
    ) {
        self.key = key
        self.frame = frame
        self.native = native
        self.pixelWidth = pixelWidth
        self.pixelHeight = pixelHeight
        self.isPrimary = isPrimary
    }

    init(_ raw: osdrop_monitor) {
        self.init(
            key: CArray.bytes(of: raw.key), frame: CoordRect(raw.frame), native: CoordRect(raw.native),
            pixelWidth: raw.pixel_width, pixelHeight: raw.pixel_height, isPrimary: raw.primary == 1)
    }

    var raw: osdrop_monitor {
        var monitor = osdrop_monitor()
        CArray.write(key, into: &monitor.key)
        monitor.frame = frame.raw
        monitor.native = native.raw
        monitor.pixel_width = pixelWidth
        monitor.pixel_height = pixelHeight
        monitor.primary = isPrimary ? 1 : 0
        return monitor
    }
}

/// Where a computer's logical origin sits in the shared arrangement.
public struct Placement: Sendable, Equatable {
    public let deviceID: DeviceID
    public var x: Int32
    public var y: Int32
    public var isDetached: Bool

    public init(deviceID: DeviceID, x: Int32, y: Int32, isDetached: Bool) {
        self.deviceID = deviceID
        self.x = x
        self.y = y
        self.isDetached = isDetached
    }

    var raw: osdrop_placement {
        var placement = osdrop_placement()
        CArray.write(deviceID.bytes, into: &placement.device_id)
        placement.x = x
        placement.y = y
        placement.detached = isDetached ? 1 : 0
        return placement
    }
}

/// A computer of the arrangement with its monitors; `placement` is `nil` until it is placed.
public struct LayoutComputer: Sendable, Equatable, Identifiable {
    public let deviceID: DeviceID
    public let placement: Placement?
    public let monitors: [ScreenMonitor]

    public var id: DeviceID { deviceID }

    init(_ raw: osdrop_layout_computer) {
        deviceID = DeviceID(field: raw.placement.device_id)
        placement =
            raw.placed == 1
            ? Placement(
                deviceID: deviceID, x: raw.placement.x, y: raw.placement.y,
                isDetached: raw.placement.detached == 1) : nil
        let all = withUnsafeBytes(of: raw.monitors) { buffer in
            Array(buffer.bindMemory(to: osdrop_monitor.self))
        }
        monitors = all.prefix(Int(raw.monitor_count)).map(ScreenMonitor.init)
    }
}
