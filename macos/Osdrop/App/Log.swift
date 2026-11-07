import os

/// Unified logging, one subsystem and a category per module. Keystrokes, clipboard contents, file
/// contents and keys are never logged; input is logged as counts only.
enum Log {
    static let subsystem = "com.akinalpfdn.osdrop"

    static let input = Logger(subsystem: subsystem, category: "input")
    static let permissions = Logger(subsystem: subsystem, category: "permissions")
    static let screens = Logger(subsystem: subsystem, category: "screens")
    static let drag = Logger(subsystem: subsystem, category: "drag")
    static let discovery = Logger(subsystem: subsystem, category: "discovery")
    static let mesh = Logger(subsystem: subsystem, category: "mesh")
    static let group = Logger(subsystem: subsystem, category: "group")
    static let clipboard = Logger(subsystem: subsystem, category: "clipboard")
    static let app = Logger(subsystem: subsystem, category: "app")
}
