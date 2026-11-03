import OsdropCore

/// What a computer announces over Bonjour (docs/PROTOCOL.md "Discovery"). Encoding and checks
/// come from core, so both platforms read and write the same TXT record.
public struct Advertisement: Sendable, Equatable {
    public static let serviceType = OSDROP_SERVICE_TYPE
    /// The same type as a Swift string, for Network.framework.
    public static let serviceTypeName: String = OSDROP_SERVICE_TYPE
    public static let protocolVersion = UInt32(OSDROP_PROTOCOL_VERSION)
    public static let protocolMinor = UInt8(OSDROP_PROTOCOL_MINOR)

    public let protocolVersion: UInt32
    public let protocolMinor: UInt8
    public let platform: PeerPlatform
    public let deviceID: DeviceID
    /// `nil` while the computer is in no group.
    public let groupID: GroupID?
    public let name: String

    /// This computer: the current protocol version and platform.
    public init(deviceID: DeviceID, groupID: GroupID?, name: String) {
        self.init(
            protocolVersion: Self.protocolVersion, protocolMinor: Self.protocolMinor, platform: .current,
            deviceID: deviceID, groupID: groupID, name: name)
    }

    public init(
        protocolVersion: UInt32, protocolMinor: UInt8, platform: PeerPlatform, deviceID: DeviceID,
        groupID: GroupID?, name: String
    ) {
        self.protocolVersion = protocolVersion
        self.protocolMinor = protocolMinor
        self.platform = platform
        self.deviceID = deviceID
        self.groupID = groupID
        self.name = name
    }

    /// A peer's record; keys it does not know are ignored, missing ones fail the checks. Another
    /// protocol version is returned, not refused, so the app can ask for an update.
    public init(txtRecord: [String: String]) throws(CoreError) {
        var record = osdrop_txt_record()
        CArray.write(txtRecord[OSDROP_TXT_KEY_PROTOCOL_VERSION] ?? "", into: &record.protocol_version)
        CArray.write(txtRecord[OSDROP_TXT_KEY_PROTOCOL_MINOR] ?? "", into: &record.protocol_minor)
        CArray.write(txtRecord[OSDROP_TXT_KEY_DEVICE_ID] ?? "", into: &record.device_id)
        CArray.write(txtRecord[OSDROP_TXT_KEY_NAME] ?? "", into: &record.name)
        CArray.write(txtRecord[OSDROP_TXT_KEY_OS] ?? "", into: &record.os)
        CArray.write(txtRecord[OSDROP_TXT_KEY_GROUP_ID] ?? "", into: &record.group_id)
        var parsed = osdrop_advertisement()
        if let error = CoreError(result: osdrop_advertisement_from_txt(&record, &parsed)) {
            throw error
        }
        self.init(
            protocolVersion: parsed.protocol_version, protocolMinor: parsed.protocol_minor,
            platform: PeerPlatform(parsed.platform), deviceID: DeviceID(field: parsed.device_id),
            groupID: parsed.has_group == 1 ? GroupID(bytes: CArray.bytes(of: parsed.group_id)) : nil,
            name: CArray.string(of: parsed.name))
    }

    /// The TXT record to register; fails for an invalid name.
    public func txtRecord() throws(CoreError) -> [String: String] {
        var advertisement = osdrop_advertisement()
        advertisement.protocol_version = protocolVersion
        advertisement.protocol_minor = protocolMinor
        advertisement.platform = platform.raw
        CArray.write(deviceID.bytes, into: &advertisement.device_id)
        if let groupID {
            advertisement.has_group = 1
            CArray.write(groupID.bytes, into: &advertisement.group_id)
        }
        guard name.utf8.count < MemoryLayout.size(ofValue: advertisement.name) else { throw .invalidString }
        CArray.write(name, into: &advertisement.name)
        var record = osdrop_txt_record()
        if let error = CoreError(result: osdrop_advertisement_to_txt(&advertisement, &record)) {
            throw error
        }
        return [
            OSDROP_TXT_KEY_PROTOCOL_VERSION: CArray.string(of: record.protocol_version),
            OSDROP_TXT_KEY_PROTOCOL_MINOR: CArray.string(of: record.protocol_minor),
            OSDROP_TXT_KEY_DEVICE_ID: CArray.string(of: record.device_id),
            OSDROP_TXT_KEY_NAME: CArray.string(of: record.name),
            OSDROP_TXT_KEY_OS: CArray.string(of: record.os),
            // Empty while in no group.
            OSDROP_TXT_KEY_GROUP_ID: CArray.string(of: record.group_id),
        ]
    }
}
