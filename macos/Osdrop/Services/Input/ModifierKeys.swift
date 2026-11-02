import CoreGraphics

/// Which modifier keys are held, with left and right told apart.
struct ModifierState: OptionSet, Hashable, Sendable {
    let rawValue: UInt8

    static let leftControl = ModifierState(rawValue: 1 << 0)
    static let leftShift = ModifierState(rawValue: 1 << 1)
    static let leftOption = ModifierState(rawValue: 1 << 2)
    static let leftCommand = ModifierState(rawValue: 1 << 3)
    static let rightControl = ModifierState(rawValue: 1 << 4)
    static let rightShift = ModifierState(rawValue: 1 << 5)
    static let rightOption = ModifierState(rawValue: 1 << 6)
    static let rightCommand = ModifierState(rawValue: 1 << 7)
}

/// The eight modifier keys: their HID usages and the CoreGraphics flags that report them.
enum ModifierKeys {
    struct Entry: Sendable {
        let usage: UInt16
        let state: ModifierState
        /// The device-dependent bit (NX_DEVICE*KEYMASK) that tells left from right.
        let deviceMask: UInt64
        /// The device-independent flag shared by both keys of the pair.
        let flag: CGEventFlags
    }

    static let capsLockUsage: UInt16 = 0x39

    static let entries: [Entry] = [
        Entry(usage: 0xE0, state: .leftControl, deviceMask: 0x0001, flag: .maskControl),
        Entry(usage: 0xE1, state: .leftShift, deviceMask: 0x0002, flag: .maskShift),
        Entry(usage: 0xE2, state: .leftOption, deviceMask: 0x0020, flag: .maskAlternate),
        Entry(usage: 0xE3, state: .leftCommand, deviceMask: 0x0008, flag: .maskCommand),
        Entry(usage: 0xE4, state: .rightControl, deviceMask: 0x2000, flag: .maskControl),
        Entry(usage: 0xE5, state: .rightShift, deviceMask: 0x0004, flag: .maskShift),
        Entry(usage: 0xE6, state: .rightOption, deviceMask: 0x0040, flag: .maskAlternate),
        Entry(usage: 0xE7, state: .rightCommand, deviceMask: 0x0010, flag: .maskCommand),
    ]

    static func entry(for key: HIDKey) -> Entry? {
        guard key.page == HIDKey.keyboardPage else { return nil }
        return entries.first { $0.usage == key.usage }
    }

    /// The flags an event must carry while these modifiers are held.
    static func flags(for state: ModifierState) -> CGEventFlags {
        var raw: UInt64 = 0
        for entry in entries where state.contains(entry.state) {
            raw |= entry.deviceMask | entry.flag.rawValue
        }
        return CGEventFlags(rawValue: raw)
    }

    /// The held modifiers described by event flags. Events from other software may carry only the
    /// device-independent flag; that counts as the left key.
    static func state(from flags: CGEventFlags) -> ModifierState {
        var state: ModifierState = []
        for entry in entries where flags.rawValue & entry.deviceMask != 0 {
            state.insert(entry.state)
        }
        for entry in entries where entry.state.isLeft && flags.contains(entry.flag) {
            let pair = entries.filter { $0.flag == entry.flag }.map(\.state)
            if state.isDisjoint(with: ModifierState(pair)) {
                state.insert(entry.state)
            }
        }
        return state
    }
}

extension ModifierState {
    fileprivate var isLeft: Bool {
        !isDisjoint(with: [.leftControl, .leftShift, .leftOption, .leftCommand])
    }
}
