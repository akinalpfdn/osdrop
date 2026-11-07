import AppKit
import Carbon.HIToolbox
import CoreGraphics

/// A key with modifiers, as Osdrop's own chords use it. The event tap matches it in both modes, so
/// it works while the input is on another computer, and no app sees it.
struct Hotkey: Hashable, Sendable, Codable {
    struct Modifiers: OptionSet, Hashable, Sendable, Codable {
        let rawValue: UInt8
        static let control = Modifiers(rawValue: 1 << 0)
        static let option = Modifiers(rawValue: 1 << 1)
        static let shift = Modifiers(rawValue: 1 << 2)
        static let command = Modifiers(rawValue: 1 << 3)
    }

    /// A virtual key code (`kVK_*`).
    let keyCode: UInt16
    let modifiers: Modifiers

    /// ⌃⌥⌘⎋: the cursor comes home, and any capture ends.
    static let defaultReturnHome = Hotkey(
        keyCode: UInt16(kVK_Escape), modifiers: [.control, .option, .command])
    /// ⌃⌥⌘L: the cursor stays on the computer it is on, or crosses again.
    static let defaultLockInput = Hotkey(keyCode: UInt16(kVK_ANSI_L), modifiers: [.control, .option, .command])

    /// A key press with at least these modifiers (more do not matter, so the chord always works).
    func matches(type: CGEventType, keyCode: Int64, flags: CGEventFlags) -> Bool {
        type == .keyDown && keyCode == Int64(self.keyCode) && flags.contains(Self.flags(of: modifiers))
    }

    private static func flags(of modifiers: Modifiers) -> CGEventFlags {
        var flags: CGEventFlags = []
        if modifiers.contains(.control) { flags.insert(.maskControl) }
        if modifiers.contains(.option) { flags.insert(.maskAlternate) }
        if modifiers.contains(.shift) { flags.insert(.maskShift) }
        if modifiers.contains(.command) { flags.insert(.maskCommand) }
        return flags
    }
}

extension Hotkey {
    /// How macOS writes shortcuts: modifiers in the order ⌃⌥⇧⌘, then the key on the current layout.
    var displayText: String {
        var text = ""
        if modifiers.contains(.control) { text += "⌃" }
        if modifiers.contains(.option) { text += "⌥" }
        if modifiers.contains(.shift) { text += "⇧" }
        if modifiers.contains(.command) { text += "⌘" }
        return text + Self.keyName(keyCode)
    }

    /// The combination a key press makes, or nil when it cannot be a chord: it needs ⌃, ⌥ or ⌘
    /// (⇧ alone would steal typing), and a modifier key alone is not a chord.
    init?(keyCode: UInt16, flags: NSEvent.ModifierFlags) {
        guard !Self.modifierKeyCodes.contains(Int(keyCode)) else { return nil }
        var modifiers: Modifiers = []
        if flags.contains(.control) { modifiers.insert(.control) }
        if flags.contains(.option) { modifiers.insert(.option) }
        if flags.contains(.shift) { modifiers.insert(.shift) }
        if flags.contains(.command) { modifiers.insert(.command) }
        guard !modifiers.isDisjoint(with: [.control, .option, .command]) else { return nil }
        self.init(keyCode: keyCode, modifiers: modifiers)
    }

    private static let modifierKeyCodes: Set<Int> = [
        kVK_Command, kVK_RightCommand, kVK_Shift, kVK_RightShift, kVK_Option, kVK_RightOption, kVK_Control,
        kVK_RightControl, kVK_CapsLock, kVK_Function,
    ]

    private static let specialKeys: [Int: String] = [
        kVK_Return: "↩", kVK_Tab: "⇥", kVK_Space: "Space", kVK_Delete: "⌫", kVK_ForwardDelete: "⌦",
        kVK_Escape: "⎋", kVK_LeftArrow: "←", kVK_RightArrow: "→", kVK_UpArrow: "↑", kVK_DownArrow: "↓",
        kVK_Home: "↖", kVK_End: "↘", kVK_PageUp: "⇞", kVK_PageDown: "⇟", kVK_F1: "F1", kVK_F2: "F2",
        kVK_F3: "F3", kVK_F4: "F4", kVK_F5: "F5", kVK_F6: "F6", kVK_F7: "F7", kVK_F8: "F8", kVK_F9: "F9",
        kVK_F10: "F10", kVK_F11: "F11", kVK_F12: "F12",
    ]

    private static func keyName(_ keyCode: UInt16) -> String {
        if let special = specialKeys[Int(keyCode)] { return special }
        return typedCharacter(keyCode)?.uppercased() ?? "#\(keyCode)"
    }

    /// The character the key types without modifiers on the current keyboard layout.
    private static func typedCharacter(_ keyCode: UInt16) -> String? {
        guard let source = TISCopyCurrentKeyboardLayoutInputSource()?.takeRetainedValue(),
            let pointer = TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData)
        else { return nil }
        let layoutData = Unmanaged<CFData>.fromOpaque(pointer).takeUnretainedValue() as Data
        return layoutData.withUnsafeBytes { buffer -> String? in
            guard let layout = buffer.baseAddress?.assumingMemoryBound(to: UCKeyboardLayout.self) else { return nil }
            var deadKeys: UInt32 = 0
            var characters = [UniChar](repeating: 0, count: 4)
            var length = 0
            let status = UCKeyTranslate(
                layout, keyCode, UInt16(kUCKeyActionDisplay), 0, UInt32(LMGetKbdType()),
                OptionBits(kUCKeyTranslateNoDeadKeysBit), &deadKeys, characters.count, &length, &characters)
            guard status == noErr, length > 0 else { return nil }
            return String(utf16CodeUnits: characters, count: length)
        }
    }
}

/// The chords the event tap keeps for Osdrop.
struct CaptureHotkeys: Equatable, Sendable {
    var returnHome = Hotkey.defaultReturnHome
    var lockInput = Hotkey.defaultLockInput
}
