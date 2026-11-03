import CoreBridge
import Testing

@Suite("Bonjour advertisement")
struct AdvertisementTests {
    private let device = DeviceID(hex: "7a91869e4c114964206972403b0a3d81")
    private let group = GroupID(hex: "00112233445566778899aabbccddeeff")

    @Test("A computer in a group round-trips through its TXT record")
    func roundTripsInAGroup() throws {
        let device = try #require(device)
        let advertisement = Advertisement(deviceID: device, groupID: group, name: "Akın's MacBook")
        let record = try advertisement.txtRecord()
        #expect(record["id"] == device.hex)
        #expect(record["grp"] == group?.hex)
        #expect(record["os"] == "macos")
        #expect(try Advertisement(txtRecord: record) == advertisement)
    }

    @Test("A computer in no group advertises an empty group")
    func roundTripsWithoutAGroup() throws {
        let device = try #require(device)
        let advertisement = Advertisement(deviceID: device, groupID: nil, name: "Mac")
        let record = try advertisement.txtRecord()
        #expect(record["grp"] == "")
        #expect(try Advertisement(txtRecord: record).groupID == nil)
    }

    @Test("Damaged records and names are refused")
    func refusesDamagedRecords() throws {
        let device = try #require(device)
        var record = try Advertisement(deviceID: device, groupID: nil, name: "Mac").txtRecord()
        record["id"] = "xyz"
        #expect(throws: CoreError.self) { try Advertisement(txtRecord: record) }
        let tooLong = Advertisement(deviceID: device, groupID: nil, name: String(repeating: "x", count: 65))
        #expect(throws: CoreError.invalidString) { try tooLong.txtRecord() }
    }

    @Test("IDs read and write lowercase hex")
    func identifiersUseHex() {
        #expect(DeviceID(hex: "7A91869E4C114964206972403B0A3D81") == nil)
        #expect(DeviceID(hex: "7a91") == nil)
        #expect(GroupID(hex: String(repeating: "0", count: 32)) == nil)
        #expect(device?.description == "7a91869e4c114964206972403b0a3d81")
    }
}
