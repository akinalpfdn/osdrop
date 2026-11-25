import CoreBridge

extension PeerPlatform {
    /// The SF Symbol that stands for a device of this platform in lists.
    var symbol: String {
        switch self {
        case .macOS: Theme.Symbol.mac
        case .windows: Theme.Symbol.pc
        case .android, .iOS: Theme.Symbol.phone
        }
    }
}
