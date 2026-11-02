// OsdropCaptureGuard: ends Osdrop when it stops answering while it holds this Mac's input.
//
// Osdrop freezes and hides the cursor while its input is used elsewhere. Those settings belong to
// Osdrop's own window server connection, so if the whole app hangs, nothing inside it can undo
// them and the user is left without a mouse. Ending the app closes the connection, and macOS
// restores the cursor at once (DEVPLAN, input safety invariants).
//
//   OsdropCaptureGuard <pid>   watch the process <pid>; messages arrive on standard input
//
// The guard exits when its standard input closes or its parent goes away.
import Darwin
import Foundation
import os

let log = Logger(subsystem: "com.akinalpfdn.osdrop", category: "guard")

guard CommandLine.arguments.count == 2, let target = pid_t(CommandLine.arguments[1]), target > 1 else {
    log.error("Usage: OsdropCaptureGuard <pid>")
    exit(EX_USAGE)
}

let parent = getppid()
let pollInterval: Int32 = 100  // milliseconds
var state = CaptureGuardState(timeout: CaptureGuardProtocol.timeout)
var buffer = [UInt8](repeating: 0, count: 64)

while true {
    var input = pollfd(fd: STDIN_FILENO, events: Int16(POLLIN), revents: 0)
    let ready = poll(&input, 1, pollInterval)
    let now = ContinuousClock.now
    if ready > 0 {
        let count = read(STDIN_FILENO, &buffer, buffer.count)
        guard count > 0 else {
            exit(EXIT_SUCCESS)  // Osdrop closed the pipe: it quit or crashed.
        }
        for byte in buffer.prefix(count) {
            state.receive(byte, at: now)
        }
    }
    if getppid() != parent {
        exit(EXIT_SUCCESS)
    }
    if state.shouldTerminate(at: now) {
        log.error("Osdrop stopped answering during input capture; ending it to give the cursor back")
        // SIGKILL also ends a stopped process; a hung app could not handle SIGTERM.
        kill(target, SIGKILL)
        exit(EXIT_SUCCESS)
    }
}
