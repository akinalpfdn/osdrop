import OsdropCore

/// A device ID: the first 16 bytes of BLAKE2b-256 over the device's public key
/// (docs/PROTOCOL.md "Conventions"). Shown and advertised as lowercase hex.
public struct DeviceID: Hashable, Comparable, Sendable, CustomStringConvertible {
    public let bytes: [UInt8]

    /// `nil` unless exactly OSDROP_DEVICE_ID_SIZE bytes.
    public init?(bytes: [UInt8]) {
        guard bytes.count == Int(OSDROP_DEVICE_ID_SIZE) else { return nil }
        self.bytes = bytes
    }

    public init?(hex: String) {
        guard let bytes = Hex.bytes(of: hex) else { return nil }
        self.init(bytes: bytes)
    }

    /// From a C field of OSDROP_DEVICE_ID_SIZE bytes.
    init<Tuple>(field: Tuple) {
        bytes = CArray.bytes(of: field)
    }

    /// Bytes core produced, OSDROP_DEVICE_ID_SIZE of them.
    init(validated bytes: [UInt8]) {
        self.bytes = bytes
    }

    public var hex: String { Hex.string(of: bytes) }
    public var description: String { hex }

    /// The order core uses to pick who dials: byte by byte.
    public static func < (lhs: DeviceID, rhs: DeviceID) -> Bool {
        lhs.bytes.lexicographicallyPrecedes(rhs.bytes)
    }
}

/// A group ID: 16 random bytes, never all zeros.
public struct GroupID: Hashable, Sendable, CustomStringConvertible {
    public let bytes: [UInt8]

    /// `nil` unless exactly OSDROP_GROUP_ID_SIZE bytes, not all zeros.
    public init?(bytes: [UInt8]) {
        guard bytes.count == Int(OSDROP_GROUP_ID_SIZE), bytes.contains(where: { $0 != 0 }) else { return nil }
        self.bytes = bytes
    }

    public init?(hex: String) {
        guard let bytes = Hex.bytes(of: hex) else { return nil }
        self.init(bytes: bytes)
    }

    public var hex: String { Hex.string(of: bytes) }
    public var description: String { hex }
}

/// Lowercase hex, as in the TXT record.
enum Hex {
    static func string(of bytes: some Sequence<UInt8>) -> String {
        bytes.map { byte in byte < 0x10 ? "0" + String(byte, radix: 16) : String(byte, radix: 16) }.joined()
    }

    static func bytes(of text: String) -> [UInt8]? {
        let digits = Array(text.utf8)
        guard digits.count.isMultiple(of: 2) else { return nil }
        var bytes: [UInt8] = []
        bytes.reserveCapacity(digits.count / 2)
        var index = 0
        while index < digits.count {
            guard let high = value(of: digits[index]), let low = value(of: digits[index + 1]) else { return nil }
            bytes.append(high << 4 | low)
            index += 2
        }
        return bytes
    }

    private static func value(of digit: UInt8) -> UInt8? {
        switch digit {
        case UInt8(ascii: "0")...UInt8(ascii: "9"): digit - UInt8(ascii: "0")
        case UInt8(ascii: "a")...UInt8(ascii: "f"): digit - UInt8(ascii: "a") + 10
        default: nil
        }
    }
}

/// C fixed-size arrays arrive in Swift as tuples; these read and write them.
enum CArray {
    static func bytes<Tuple>(of field: Tuple) -> [UInt8] {
        withUnsafeBytes(of: field) { Array($0) }
    }

    static func write<Tuple>(_ bytes: [UInt8], into field: inout Tuple) {
        withUnsafeMutableBytes(of: &field) { buffer in
            buffer.initializeMemory(as: UInt8.self, repeating: 0)
            buffer.copyBytes(from: bytes.prefix(buffer.count))
        }
    }

    /// A NUL-terminated string.
    static func string<Tuple>(of field: Tuple) -> String {
        withUnsafeBytes(of: field) { buffer in
            String(decoding: buffer.prefix { $0 != 0 }, as: UTF8.self)
        }
    }

    /// Writes as much of `text` as fits with its terminating NUL.
    static func write<Tuple>(_ text: String, into field: inout Tuple) {
        withUnsafeMutableBytes(of: &field) { buffer in
            buffer.initializeMemory(as: UInt8.self, repeating: 0)
            buffer.copyBytes(from: Array(text.utf8.prefix(buffer.count - 1)))
        }
    }
}
