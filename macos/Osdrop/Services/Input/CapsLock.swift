import IOKit
import IOKit.hidsystem

/// Caps Lock on this Mac. It travels between computers as a state (DEVPLAN Phase 12): a posted
/// Caps Lock key event does not toggle it on macOS.
protocol CapsLockControlling: Sendable {
    var isOn: Bool { get }
    func set(_ on: Bool)
}

/// Reads and sets Caps Lock through the HID system's parameter connection.
final class HIDCapsLock: CapsLockControlling {
    private let connection: io_connect_t

    init() {
        let service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching(kIOHIDSystemClass))
        var connection: io_connect_t = 0
        if service != 0 {
            let result = IOServiceOpen(service, mach_task_self_, UInt32(kIOHIDParamConnectType), &connection)
            if result != KERN_SUCCESS {
                Log.input.error("Opening the HID system failed: \(result, privacy: .public)")
            }
            IOObjectRelease(service)
        }
        self.connection = connection
    }

    deinit {
        if connection != 0 {
            IOServiceClose(connection)
        }
    }

    var isOn: Bool {
        var state = false
        guard connection != 0,
            IOHIDGetModifierLockState(connection, Int32(kIOHIDCapsLockState), &state) == KERN_SUCCESS
        else {
            return false
        }
        return state
    }

    func set(_ on: Bool) {
        guard connection != 0 else { return }
        let result = IOHIDSetModifierLockState(connection, Int32(kIOHIDCapsLockState), on)
        if result != KERN_SUCCESS {
            Log.input.error("Setting Caps Lock failed: \(result, privacy: .public)")
        }
    }
}
