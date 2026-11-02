import AppKit

/// Media and brightness keys. macOS reports them as system-defined events carrying an NX key type;
/// other computers know them as HID consumer-page usages.
enum MediaKeys {
    /// Subtype of the system-defined events that carry auxiliary keys (NX_SUBTYPE_AUX_CONTROL_BUTTONS).
    static let auxiliaryControlSubtype: Int16 = 8
    /// Raw CoreGraphics event type of system-defined events (NX_SYSDEFINED).
    static let systemDefinedEventType: UInt32 = 14

    private static let keyStateDown = 0xA
    private static let keyStateUp = 0xB

    /// NX key type (NX_KEYTYPE_*) and HID consumer usage of every media key both sides know.
    static let pairs: [(keyType: Int, usage: UInt16)] = [
        (0, 0xE9),  // sound up
        (1, 0xEA),  // sound down
        (2, 0x6F),  // brightness up
        (3, 0x70),  // brightness down
        (7, 0xE2),  // mute
        (14, 0xB8),  // eject
        (16, 0xCD),  // play / pause
        (17, 0xB5),  // next track
        (18, 0xB6),  // previous track
        (19, 0xB3),  // fast forward
        (20, 0xB4),  // rewind
        (21, 0x79),  // keyboard illumination up
        (22, 0x7A),  // keyboard illumination down
        (23, 0x7C),  // keyboard illumination toggle
    ]

    static func hidKey(forKeyType keyType: Int) -> HIDKey? {
        pairs.first { $0.keyType == keyType }.map { HIDKey.consumer($0.usage) }
    }

    static func keyType(for key: HIDKey) -> Int? {
        guard key.page == HIDKey.consumerPage else { return nil }
        return pairs.first { $0.usage == key.usage }?.keyType
    }

    /// The media key and action a system-defined event carries, or nil for other events.
    static func decode(_ event: CGEvent) -> (key: HIDKey, action: KeyAction)? {
        guard event.type.rawValue == systemDefinedEventType, let nsEvent = NSEvent(cgEvent: event),
            nsEvent.subtype.rawValue == auxiliaryControlSubtype
        else {
            return nil
        }
        let data = nsEvent.data1
        let keyType = (data & 0xFFFF_0000) >> 16
        let keyState = (data & 0xFF00) >> 8
        let isRepeat = data & 0x1 != 0
        guard let key = hidKey(forKeyType: keyType), keyState == keyStateDown || keyState == keyStateUp else {
            return nil
        }
        if keyState == keyStateUp {
            return (key, .up)
        }
        return (key, isRepeat ? .repeated : .down)
    }

    /// A system-defined event that presses or releases a media key.
    static func makeEvent(for key: HIDKey, isDown: Bool) -> CGEvent? {
        guard let keyType = keyType(for: key) else { return nil }
        let keyState = isDown ? keyStateDown : keyStateUp
        return NSEvent.otherEvent(
            with: .systemDefined, location: .zero, modifierFlags: NSEvent.ModifierFlags(rawValue: UInt(keyState << 8)),
            timestamp: 0, windowNumber: 0, context: nil, subtype: auxiliaryControlSubtype,
            data1: (keyType << 16) | (keyState << 8), data2: -1
        )?.cgEvent
    }
}
