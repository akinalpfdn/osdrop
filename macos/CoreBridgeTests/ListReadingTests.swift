import OsdropCore
import Testing

@testable import CoreBridge

/// Lists core fills in: members and the arrangement can grow between asking for their size and
/// reading them, when another member's state arrives.
@Suite("Lists core fills in")
struct ListReadingTests {
    @Test("A list that grows while it is read is read again")
    func readsAgain() throws {
        let sizes = [2, 3, 3]
        var calls = 0
        let read = try Mesh.readList(of: Int32(0)) { buffer, capacity, count in
            let size = sizes[min(calls, sizes.count - 1)]
            calls += 1
            count.pointee = size
            guard capacity >= size, let buffer else { return OSDROP_ERROR_BUFFER_TOO_SMALL }
            for index in 0..<size {
                buffer[index] = Int32(index)
            }
            return OSDROP_OK
        }
        #expect(read == [0, 1, 2])
        #expect(calls == 3)
    }

    @Test("A list that keeps growing gives up")
    func givesUp() {
        #expect(throws: CoreError.bufferTooSmall) {
            try Mesh.readList(of: Int32(0)) { _, capacity, count in
                count.pointee = capacity + 1
                return OSDROP_ERROR_BUFFER_TOO_SMALL
            }
        }
    }

    @Test("Other failures are thrown, and an empty list is read")
    func failures() throws {
        #expect(throws: CoreError.invalidState) {
            try Mesh.readList(of: Int32(0)) { _, _, _ in OSDROP_ERROR_INVALID_STATE }
        }
        #expect(try Mesh.readList(of: Int32(0)) { _, _, _ in OSDROP_OK }.isEmpty)
    }
}
