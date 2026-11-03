import OsdropCore

/// Which wire format a byte buffer holds (docs/PROTOCOL.md).
public enum WireKind: Sendable {
    /// One control message, without the TCP length prefix.
    case controlMessage
    /// One plaintext input datagram, without the AEAD tag.
    case inputDatagram
    /// One transfer message, without the TCP length prefix.
    case transferMessage

    var raw: osdrop_wire_kind {
        switch self {
        case .controlMessage: OSDROP_WIRE_CONTROL_MESSAGE
        case .inputDatagram: OSDROP_WIRE_INPUT_DATAGRAM
        case .transferMessage: OSDROP_WIRE_TRANSFER_MESSAGE
        }
    }
}

/// Decodes protocol bytes with the core's codec, for diagnostics and tests.
public enum ProtocolDiagnostics {
    /// The message or datagram type, if the bytes pass every check the session applies.
    public static func validate(_ bytes: [UInt8], as kind: WireKind) -> Result<UInt8, CoreError> {
        var type: UInt8 = 0
        let result = bytes.withUnsafeBufferPointer { buffer in
            osdrop_protocol_validate(kind.raw, buffer.baseAddress, buffer.count, &type)
        }
        if let error = CoreError(result: result) {
            return .failure(error)
        }
        return .success(type)
    }

    /// Decodes and re-encodes; a valid input comes back byte for byte.
    public static func reencode(_ bytes: [UInt8], as kind: WireKind) -> Result<[UInt8], CoreError> {
        let capacity = max(Int(OSDROP_MAX_CONTROL_MESSAGE_SIZE), Int(OSDROP_MAX_TRANSFER_MESSAGE_SIZE))
        var output = [UInt8](repeating: 0, count: capacity)
        var length = 0
        let result = bytes.withUnsafeBufferPointer { input in
            output.withUnsafeMutableBufferPointer { out in
                osdrop_protocol_reencode(kind.raw, input.baseAddress, input.count, out.baseAddress, out.count, &length)
            }
        }
        if let error = CoreError(result: result) {
            return .failure(error)
        }
        return .success(Array(output.prefix(length)))
    }
}
