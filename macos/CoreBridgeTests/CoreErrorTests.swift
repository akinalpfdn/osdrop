import CoreBridge
import OsdropCore
import Testing

@Suite("CoreError mapping")
struct CoreErrorTests {
    @Test("OK is not an error")
    func okIsNil() {
        #expect(CoreError(result: OSDROP_OK) == nil)
    }

    @Test("Every known result code maps to its own case and back", arguments: 1...31)
    func knownCodesRoundTrip(_ code: Int) throws {
        let error = try #require(CoreError(result: osdrop_result(rawValue: UInt32(code))))
        #expect(error.code == UInt32(code))
        #expect(error != .unknown(code: UInt32(code)))
    }

    @Test("Codes from a newer core are kept")
    func unknownCodesArePreserved() {
        #expect(CoreError(result: osdrop_result(rawValue: 4242)) == .unknown(code: 4242))
    }
}
