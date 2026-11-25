import AppKit
import CoreBridge
import Observation

/// The phones' boxes in screen corners (DEVPLAN Phase 21): while a drag is on this Mac, each
/// connected phone's corner takes it, and the box shows once the drag reaches the corner; files
/// dropped in go to the phone. Files a phone sends open its box by themselves, to be dragged out.
@MainActor
final class CornerBoxController {
    private let group: GroupController
    private let settings: AppSettings
    private let staging: DragStaging
    private let monitor = DragActivityMonitor()
    private var boxes: [DeviceID: CornerBox] = [:]
    /// The drag in progress started in a box: no corner takes it.
    private var draggingOut = false
    private var isWatching = false

    init(group: GroupController, settings: AppSettings, staging: DragStaging) {
        self.group = group
        self.settings = settings
        self.staging = staging
    }

    func start() {
        monitor.onChange = { [weak self] dragging in self?.dragChanged(dragging) }
        group.onPhoneSend = { [weak self] phone in self?.box(for: phone)?.open() }
        watchMembers()
    }

    /// Drags are watched only while a phone is connected.
    private func watchMembers() {
        let phones = withObservationTracking {
            group.members.filter { $0.platform.isPhone && $0.isConnected }.map(\.deviceID)
        } onChange: { [weak self] in
            Task { @MainActor in self?.watchMembers() }
        }
        if phones.isEmpty, isWatching {
            monitor.stop()
            isWatching = false
        } else if !phones.isEmpty, !isWatching {
            monitor.start()
            isWatching = true
        }
        for (phone, box) in boxes where !phones.contains(phone) {
            box.dragEnded()  // it takes nothing while the phone is away
        }
    }

    private func dragChanged(_ dragging: Bool) {
        guard dragging else {
            draggingOut = false
            boxes.values.forEach { $0.dragEnded() }
            return
        }
        guard !draggingOut else { return }
        for member in group.members where member.platform.isPhone && member.isConnected {
            box(for: member.deviceID)?.dragStarted()
        }
    }

    private func box(for phone: DeviceID) -> CornerBox? {
        guard let transfers = group.transfers else { return nil }
        if let box = boxes[phone] {
            if box.transfers === transfers {
                return box
            }
            box.close()  // the mesh started again with a new transfers list
        }
        let box = CornerBox(
            phone: phone, name: { [group] in group.name(of: phone) }, transfers: transfers, staging: staging,
            place: { [settings] in settings.corner(of: phone) })
        box.onDrop = { [weak self] urls in self?.group.sendFiles(urls, to: phone) }
        box.onDragOut = { [weak self] in self?.draggingOut = true }
        boxes[phone] = box
        return box
    }
}
