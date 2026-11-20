// Compiled into the Osdrop app and into OsdropNetworkHelper, so both sides share one definition.

import Foundation

/// The privileged helper's service, as the app calls it over XPC (PHASE-024-NET-01).
@objc protocol NetworkHelperService {
    /// While `off` is true and this connection lives, AWDL is kept off; the reply says whether it
    /// is off now. The helper lets AWDL up again when the last connection asking for it goes.
    func keepAWDLOff(_ off: Bool, reply: @escaping @Sendable (Bool) -> Void)
}

enum NetworkHelper {
    static let label = "com.akinalpfdn.osdrop.network-helper"
    static let plistName = "\(label).plist"
    static let appIdentifier = "com.akinalpfdn.osdrop"
    static let interface = "awdl0"

    /// Only Osdrop's own app, signed by the same team as the helper, may call it. Nil without a
    /// team (an ad hoc build): the helper then answers nobody.
    static func clientRequirement(team: String?) -> String? {
        guard let team, !team.isEmpty, team.allSatisfy({ $0.isLetter || $0.isNumber }) else { return nil }
        return "anchor apple generic and identifier \"\(appIdentifier)\" and certificate leaf[subject.OU] = \"\(team)\""
    }
}

/// When the helper turns AWDL off and lets it up again, kept free of I/O so it can be tested.
/// AWDL is turned off while anyone asks for it; it comes back when the last one lets go, or when
/// the helper starts and finds a mark that it was holding AWDL off (it crashed meanwhile).
struct AWDLHold: Sendable, Equatable {
    enum Action: Sendable, Equatable {
        case turnOff
        case turnOn
    }

    private(set) var holders: Set<Int> = []
    /// AWDL was on when the holding began: it is turned on again at the end. Kept on disk too.
    private(set) var restoresOn = false

    var isHolding: Bool { !holders.isEmpty }

    /// The helper started; `markedOn` is the mark a previous run left, if any.
    mutating func started(markedOn: Bool) -> Action? {
        restoresOn = false
        return markedOn ? .turnOn : nil
    }

    mutating func hold(_ client: Int, isOn: Bool) -> Action? {
        let first = holders.isEmpty
        holders.insert(client)
        if first {
            restoresOn = isOn
        }
        return isOn ? .turnOff : nil
    }

    mutating func release(_ client: Int) -> Action? {
        guard holders.remove(client) != nil, holders.isEmpty else { return nil }
        defer { restoresOn = false }
        return restoresOn ? .turnOn : nil
    }

    /// The system changed the interface: while holding, AWDL that came back goes off again, and
    /// is turned on at the end, as the system wanted it.
    mutating func changed(isOn: Bool) -> Action? {
        guard isHolding, isOn else { return nil }
        restoresOn = true
        return .turnOff
    }
}
