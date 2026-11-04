import Foundation
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

    public init(
        deviceID: DeviceID, publicKey: [UInt8], platform: PeerPlatform, protocolMinor: UInt8, groupID: GroupID?,
        name: String
    ) {
        self.deviceID = deviceID
        self.publicKey = publicKey
        self.platform = platform
        self.protocolMinor = protocolMinor
        self.groupID = groupID
        self.name = name
    }

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

    public init(
        deviceID: DeviceID, publicKey: [UInt8], platform: PeerPlatform, name: String,
        permissions: MemberPermissions, isSelf: Bool, isConnected: Bool
    ) {
        self.deviceID = deviceID
        self.publicKey = publicKey
        self.platform = platform
        self.name = name
        self.permissions = permissions
        self.isSelf = isSelf
        self.isConnected = isConnected
    }

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
    /// An offered item was scanned: its size, or why it cannot be sent.
    case offerReady(TransferItem, Result<OfferSummary, CoreError>)
    /// At most every 100 ms while a transfer this computer receives makes progress.
    case transferProgress(TransferProgress)
    /// The end of a transfer this computer receives; after a failure nothing it created is left.
    case transferFinished(id: UInt64, Result<ReceivedItem, CoreError>)
    /// Another member's copy is now the newest; its formats can be fetched with
    /// `TransferItem(kind: .clipboard, objectID: clipID, index: format.code)`.
    case clipboardChanged(DeviceID, clipID: UInt64, formats: [AnnouncedFormat])
    /// The end of a `fetchContent`.
    case contentReceived(id: UInt64, Result<Data, CoreError>)
}

/// A format of the shared clipboard (docs/PROTOCOL.md "0x40 ClipboardAnnounce").
public enum ClipboardFormat: Sendable, Hashable, CaseIterable {
    /// UTF-8.
    case text
    case rtf
    /// UTF-8.
    case html
    /// PNG.
    case image
    /// Fetched with `receive`.
    case files

    /// The format code, the item index of its transfers.
    public var code: UInt32 { UInt32(raw.rawValue) }

    var raw: osdrop_clipboard_format {
        switch self {
        case .text: OSDROP_CLIPBOARD_TEXT
        case .rtf: OSDROP_CLIPBOARD_RTF
        case .html: OSDROP_CLIPBOARD_HTML
        case .image: OSDROP_CLIPBOARD_IMAGE
        case .files: OSDROP_CLIPBOARD_FILES
        }
    }

    init?(_ raw: osdrop_clipboard_format) {
        guard let format = Self.allCases.first(where: { $0.raw == raw }) else { return nil }
        self = format
    }
}

/// One format of another member's copy, as announced.
public struct AnnouncedFormat: Sendable, Equatable {
    public let format: ClipboardFormat
    /// For `.files`, the total of the files.
    public let byteCount: UInt64

    public init(format: ClipboardFormat, byteCount: UInt64) {
        self.format = format
        self.byteCount = byteCount
    }
}

/// One format of a copy on this computer, with its bytes.
public struct ClipboardContent: Sendable, Equatable {
    /// `.text`, `.rtf`, `.html` or `.image`; files are passed as URLs.
    public let format: ClipboardFormat
    public let data: Data

    public init(format: ClipboardFormat, data: Data) {
        self.format = format
        self.data = data
    }
}

/// One item a transfer moves (docs/PROTOCOL.md "Transfer streams").
public struct TransferItem: Sendable, Hashable {
    public enum Kind: Sendable, Hashable {
        /// `objectID` is the drag ID, `index` the item in the drag.
        case drag
        /// `objectID` is the clip ID, `index` the format code.
        case clipboard
    }

    public let kind: Kind
    public let objectID: UInt64
    public let index: UInt32

    public init(kind: Kind, objectID: UInt64, index: UInt32) {
        self.kind = kind
        self.objectID = objectID
        self.index = index
    }

    init(_ raw: osdrop_object_ref) {
        self.init(
            kind: raw.kind == OSDROP_OBJECT_CLIPBOARD ? .clipboard : .drag, objectID: raw.object_id,
            index: raw.item_index)
    }

    var raw: osdrop_object_ref {
        osdrop_object_ref(kind: kind.raw, object_id: objectID, item_index: index)
    }
}

extension TransferItem.Kind {
    var raw: osdrop_object_kind {
        switch self {
        case .drag: OSDROP_OBJECT_DRAG
        case .clipboard: OSDROP_OBJECT_CLIPBOARD
        }
    }
}

/// What a scanned offer holds.
public struct OfferSummary: Sendable, Equatable {
    public let totalBytes: UInt64
    public let entryCount: UInt64
    /// Entries that cannot travel (names the protocol refuses, devices, long link targets).
    public let skipped: UInt64
}

public struct TransferProgress: Sendable, Equatable {
    public let id: UInt64
    public let bytesDone: UInt64
    public let bytesTotal: UInt64
    public let filesDone: UInt64
    public let filesTotal: UInt64

    init(_ raw: osdrop_transfer_progress) {
        id = raw.transfer_id
        bytesDone = raw.bytes_done
        bytesTotal = raw.bytes_total
        filesDone = raw.files_done
        filesTotal = raw.files_total
    }
}

/// What a finished transfer created.
public struct ReceivedItem: Sendable, Equatable {
    /// The item's top-level files and folders, as created (names already taken are numbered).
    public let items: [URL]
    /// Links not recreated: they pointed outside the item.
    public let skippedLinks: UInt64
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
