import Testing

@testable import Osdrop

/// The network helper's decisions, without root: when AWDL goes off and when it comes back, and
/// which callers it answers.
@Suite("Keeping AWDL off")
struct NetworkHelperTests {
    @Test("AWDL goes off with the first hold and comes back when the last one lets go")
    func holdsAndRestores() {
        var hold = AWDLHold()
        #expect(hold.hold(1, isOn: true) == .turnOff)
        #expect(hold.hold(2, isOn: false) == nil, "already off")
        #expect(hold.release(1) == nil, "another one still holds it")
        #expect(hold.release(2) == .turnOn)
        #expect(!hold.isHolding)
        #expect(hold.release(2) == nil, "a client that let go already")
    }

    @Test("AWDL that was off before stays off afterwards")
    func keepsWhatItFound() {
        var hold = AWDLHold()
        #expect(hold.hold(1, isOn: false) == nil)
        #expect(hold.release(1) == nil)
    }

    @Test("AWDL the system brings back while held goes off again, and is on at the end")
    func turnsOffWhatComesBack() {
        var hold = AWDLHold()
        #expect(hold.changed(isOn: true) == nil, "nobody holds it")
        #expect(hold.hold(1, isOn: false) == nil)
        #expect(hold.changed(isOn: true) == .turnOff)
        #expect(hold.changed(isOn: false) == nil)
        #expect(hold.release(1) == .turnOn)
    }

    @Test("A helper that finds its mark lets AWDL up: the run before ended while holding it")
    func restoresAfterACrash() {
        var hold = AWDLHold()
        #expect(hold.started(markedOn: true) == .turnOn)
        #expect(hold.started(markedOn: false) == nil)
        #expect(!hold.restoresOn)
    }

    @Test("Only Osdrop's app signed by the helper's team may call it")
    func clientRequirement() {
        #expect(
            NetworkHelper.clientRequirement(team: "WQ54PPL5VQ")
                == "anchor apple generic and identifier \"com.akinalpfdn.osdrop\" and certificate leaf[subject.OU] = \"WQ54PPL5VQ\""
        )
        #expect(NetworkHelper.clientRequirement(team: nil) == nil, "an ad hoc build answers nobody")
        #expect(NetworkHelper.clientRequirement(team: "") == nil)
        #expect(NetworkHelper.clientRequirement(team: "X\" or true") == nil, "no way out of the quotes")
    }
}
