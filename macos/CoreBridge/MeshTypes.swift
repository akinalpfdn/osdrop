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

/// A position in 1/16 point of a computer's native desktop (docs/PROTOCOL.md "Conventions").
public struct CoordPoint: Sendable, Equatable {
    public var x: Int32
    public var y: Int32

    public init(x: Int32, y: Int32) {
        self.x = x
        self.y = y
    }
}

/// Scroll fields with their protocol codes (docs/PROTOCOL.md "0x25 ScrollEvent").
public struct ScrollStroke: Sendable, Equatable {
    /// 1 lines (wheels), 2 points (trackpads, continuous wheels).
    public var unit: UInt8
    /// 0 none, 1 may begin, 2 began, 3 changed, 4 ended, 5 cancelled.
    public var phase: UInt8
    /// 0 none, 1 began, 2 changed, 3 ended.
    public var momentum: UInt8
    /// 1/256 of a unit; positive moves content right and down.
    public var dx: Int32
    public var dy: Int32

    public init(unit: UInt8, phase: UInt8, momentum: UInt8, dx: Int32, dy: Int32) {
        self.unit = unit
        self.phase = phase
        self.momentum = momentum
        self.dx = dx
        self.dy = dy
    }
}

/// One input event: physical input this computer captured, or input to inject here.
public enum ControlInput: Sendable, Equatable {
    /// Physical: the cursor is at `position` and moved by `delta`. To inject: move the cursor to
    /// `position` (`delta` is zero).
    case pointer(position: CoordPoint, delta: CoordPoint)
    /// `page` 7 keyboard, 12 consumer; `action` 1 down, 2 up, 3 repeat.
    case key(page: UInt16, usage: UInt16, action: UInt8)
    /// `button` 0 left, 1 right, 2 middle, 3-31 others. `position` is where it happens when
    /// injected; physical buttons use the cursor's position.
    case button(UInt8, isDown: Bool, clickCount: UInt8, position: CoordPoint)
    case scroll(ScrollStroke, position: CoordPoint)
    /// To inject only: Caps Lock travels as a state, not as key presses.
    case capsLock(Bool)

    init(_ raw: osdrop_input) {
        let position = CoordPoint(x: raw.x, y: raw.y)
        switch raw.kind {
        case OSDROP_INPUT_KEY:
            self = .key(page: raw.page, usage: raw.usage, action: raw.action)
        case OSDROP_INPUT_BUTTON:
            self = .button(raw.button, isDown: raw.action == 1, clickCount: raw.click_count, position: position)
        case OSDROP_INPUT_CAPS_LOCK:
            self = .capsLock(raw.action == 1)
        case OSDROP_INPUT_SCROLL:
            self = .scroll(
                ScrollStroke(
                    unit: raw.scroll_unit, phase: raw.scroll_phase, momentum: raw.momentum, dx: raw.dx, dy: raw.dy),
                position: position)
        default:
            self = .pointer(position: position, delta: CoordPoint(x: raw.dx, y: raw.dy))
        }
    }

    var raw: osdrop_input {
        var raw = osdrop_input()
        switch self {
        case .pointer(let position, let delta):
            raw.kind = OSDROP_INPUT_POINTER
            (raw.x, raw.y, raw.dx, raw.dy) = (position.x, position.y, delta.x, delta.y)
        case .key(let page, let usage, let action):
            raw.kind = OSDROP_INPUT_KEY
            (raw.page, raw.usage, raw.action) = (page, usage, action)
        case .button(let button, let isDown, let clickCount, let position):
            raw.kind = OSDROP_INPUT_BUTTON
            (raw.button, raw.action, raw.click_count) = (button, isDown ? 1 : 2, clickCount)
            (raw.x, raw.y) = (position.x, position.y)
        case .scroll(let stroke, let position):
            raw.kind = OSDROP_INPUT_SCROLL
            (raw.scroll_unit, raw.scroll_phase, raw.momentum) = (stroke.unit, stroke.phase, stroke.momentum)
            (raw.dx, raw.dy, raw.x, raw.y) = (stroke.dx, stroke.dy, position.x, position.y)
        case .capsLock(let on):
            raw.kind = OSDROP_INPUT_CAPS_LOCK
            raw.action = on ? 1 : 2
        }
        return raw
    }
}

/// Why another computer refused control (docs/PROTOCOL.md "0x22 Refuse").
public enum ControlRefusal: Sendable, Equatable {
    case locked
    case asleep
    case notPermitted
    case busy

    init?(_ code: UInt8) {
        switch code {
        case 1: self = .locked
        case 2: self = .asleep
        case 3: self = .notPermitted
        case 4: self = .busy
        default: return nil
        }
    }
}

/// Why the cursor came back to this computer.
public enum CaptureEnd: Sendable, Equatable {
    /// Pushed back through an edge.
    case returned
    case hotkey
    /// The capture was lost.
    case inputLost
    case refused(ControlRefusal)
    /// Someone used the other computer's own mouse or keyboard.
    case takeover
    /// The other computer stopped answering, left, or stopped accepting control.
    case peerLost

    init(reason: UInt8, refused: UInt8) {
        switch reason {
        case 1: self = .returned
        case 2: self = .hotkey
        case 3: self = .inputLost
        case 4: self = .refused(ControlRefusal(refused) ?? .notPermitted)
        case 5: self = .takeover
        default: self = .peerLost
        }
    }
}

/// How modifiers travel between a Mac and a PC; between computers of one platform they never
/// change.
public enum ModifierMapping: Sendable, Equatable {
    /// Every key as it is: ⌘ is the Windows key.
    case positions
    /// Control and Command swapped, so ⌘C copies on a PC (the default; user decision, Phase 12).
    case shortcuts
}

/// The user's input settings (DEVPLAN Phase 12).
public struct InputSettings: Sendable, Equatable {
    public static let maxPushMilliseconds: UInt32 = 5_000

    public var modifierMapping: ModifierMapping
    /// How long a push against an edge must last before the cursor crosses.
    public var pushMilliseconds: UInt32
    /// Cross as soon as the cursor pushes past an edge.
    public var immediate: Bool
    /// Enter modifier bits (bit 0 left Control … bit 7 right Command): the cursor crosses only
    /// while one of them is held; 0: always.
    public var crossingModifiers: UInt8

    public init(
        modifierMapping: ModifierMapping = .shortcuts, pushMilliseconds: UInt32 = 1_000, immediate: Bool = false,
        crossingModifiers: UInt8 = 0
    ) {
        self.modifierMapping = modifierMapping
        self.pushMilliseconds = pushMilliseconds
        self.immediate = immediate
        self.crossingModifiers = crossingModifiers
    }

    var raw: osdrop_input_settings {
        osdrop_input_settings(
            modifier_mapping: modifierMapping == .shortcuts ? 1 : 0, immediate: immediate ? 1 : 0,
            crossing_modifiers: crossingModifiers, push_ms: pushMilliseconds)
    }
}

/// Why this computer cannot be controlled now.
public enum ControlUnavailability: Sendable, Equatable {
    case screenLocked
    case asleep
    /// This computer may not inject input: a permission is missing.
    case notPermitted

    var code: UInt8 {
        switch self {
        case .screenLocked: 1
        case .asleep: 2
        case .notPermitted: 3
        }
    }
}

/// Input control events, in order, from core's I/O thread (docs/PROTOCOL.md "Input control
/// messages").
public enum ControlEvent: Sendable, Equatable {
    /// The cursor went to `device`: capture this Mac's input and hide its cursor; release the keys
    /// in `releaseHere` on this Mac (key events with action 2).
    case captureBegan(DeviceID, releaseHere: [ControlInput])
    /// The cursor came back from `device`: give the input back, show the cursor at `cursor` and
    /// set Caps Lock as the user left it on the other computers.
    case captureEnded(DeviceID, cursor: CoordPoint, reason: CaptureEnd, capsLock: Bool)
    /// `controller` controls this computer now; `nil` when the control ended.
    case controlled(by: DeviceID?)
    /// Input from the controller to apply here.
    case inject(ControlInput)
    /// While controlled: motion latency from the controller's capture to here, in microseconds.
    case inputLatency(DeviceID, samples: UInt32, medianMicroseconds: UInt32, p99Microseconds: UInt32)
    /// A drag came to this Mac with the cursor: start a drag of `items` at `point`, pressing the
    /// left button there. Fetch each item from `source` when the drop asks for it (kind `.drag`,
    /// object ID `dragID`, index its place in `items`), and report the end with `dropFinished`.
    case dragArrived(source: DeviceID, dragID: UInt32, at: CoordPoint, items: [ArrivingItem])
    /// The drag `dragID` left this Mac, or cannot go on: end it without a drop. A held left
    /// button is released afterwards.
    case dragWithdrawn(dragID: UInt32)
    /// This Mac's drag `dragID` waits at the edge where it left: release the left button there,
    /// as a copy when it was dropped, sliding back otherwise.
    case dragEnded(dragID: UInt32, DropOutcome)
}

/// A file or folder of a drag that came to this Mac (docs/PROTOCOL.md "0x31 DragOffer").
public struct ArrivingItem: Sendable, Equatable {
    /// One name, in NFC.
    public let name: String
    public let isFolder: Bool
    /// The file's size, or the total of the files in the folder.
    public let byteCount: UInt64
    /// A Uniform Type Identifier.
    public let contentType: String

    public init(name: String, isFolder: Bool, byteCount: UInt64, contentType: String) {
        self.name = name
        self.isFolder = isFolder
        self.byteCount = byteCount
        self.contentType = contentType
    }
}

/// A file or folder of a drag on this Mac.
public struct DraggedFile: Sendable, Equatable {
    public let url: URL
    /// A Uniform Type Identifier; core replaces one it cannot send.
    public let contentType: String

    public init(url: URL, contentType: String) {
        self.url = url
        self.contentType = contentType
    }
}

/// How a drag ended (docs/PROTOCOL.md "0x32 DragResult").
public enum DropOutcome: Sendable, Equatable {
    /// The drop took the items.
    case dropped
    /// Nothing took them: Escape, or a place that takes nothing.
    case cancelled
    /// The drag could not start.
    case failed

    init(_ raw: osdrop_drag_outcome) {
        switch raw {
        case OSDROP_DRAG_DROPPED: self = .dropped
        case OSDROP_DRAG_FAILED: self = .failed
        default: self = .cancelled
        }
    }

    var raw: osdrop_drag_outcome {
        switch self {
        case .dropped: OSDROP_DRAG_DROPPED
        case .cancelled: OSDROP_DRAG_CANCELLED
        case .failed: OSDROP_DRAG_FAILED
        }
    }
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

    public init(items: [URL], skippedLinks: UInt64) {
        self.items = items
        self.skippedLinks = skippedLinks
    }
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

    public init(deviceID: DeviceID, placement: Placement?, monitors: [ScreenMonitor]) {
        self.deviceID = deviceID
        self.placement = placement
        self.monitors = monitors
    }

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
