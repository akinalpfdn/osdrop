# Architecture

> Status: outline. Sections are filled in by the phases that build each part; the full design is in
> [`DEVPLAN.md`](../DEVPLAN.md) → Architecture.

## Overview

Paired computers form a group with one shared arrangement and keep a full mesh of encrypted
sessions. The computer whose mouse and keyboard are in use is the **controller**; it decides with
the shared crossing engine in `core` which computer receives input. The others are **endpoints**
that inject what they receive.

```
CONTROLLER                                                     ENDPOINT
Event tap / hooks ─► capture ─► core: crossing engine ─┬─UDP motion (latest wins)─► injector
                                                       └─TCP keys, buttons, scroll─►
                                                     ◄──TCP cursor reports, refusals, takeover
```

## Repository layout

| Path | Contents |
|------|----------|
| `core/` | Shared C++20 library with a flat C API (`core/include/osdrop/osdrop.h`) |
| `Package.swift` | Exposes `core` to Swift as `OsdropCore` and its wrapper `CoreBridge` |
| `macos/` | Swift menu bar app (generated with XcodeGen from `macos/project.yml`) and `CoreBridge` |
| `windows/engine/` | Native engine DLL (`OsdropEngine.dll`) used by the shell |
| `windows/input/` | `OsdropInput.exe`: input capture and injection as SYSTEM in the user's session |
| `windows/service/` | `OsdropService`: keeps `OsdropInput` running in the active session |
| `windows/shared/` | Windows-header-free libraries, built and tested on every host |
| `windows/app/` | WPF shell (`Osdrop.App`) and its strings (`Osdrop.Localization`) |
| `windows/installer/` | Inno Setup script |
| `protocol/` | Binary fixtures shared by every test suite |
| `docs/` | This documentation |

## Build graph

- `core` is a static library. CMake (`core/CMakeLists.txt`) is the reference build; SwiftPM
  (`Package.swift`) compiles the same sources for the macOS app.
- The macOS app links `core` through the `CoreBridge` product of the local Swift package.
- `windows/CMakeLists.txt` adds `core` and `windows/shared` and, on Windows, the engine, input
  process and service. On other hosts it builds the portable parts only (`portable` preset).
- The WPF shell loads the engine DLL through P/Invoke.

## Core networking

Built in Phases 5–7 (`core/src`). Everything that decides what goes on the wire is sans-I/O:
bytes, datagrams, time and commands go in; bytes, datagrams and callbacks come out. Tests drive
it with a simulated clock and network; one Asio layer drives it in the apps.

| Layer | Directory | Role |
|-------|-----------|------|
| Protocol | `protocol/` | Strict decoders and canonical encoders for every message ([`PROTOCOL.md`](PROTOCOL.md)) |
| Crypto | `crypto/` | Noise XX, cipher states, session and stream keys, input datagram sealing, pairing code |
| Session | `session/` | `SessionEngine`: one control connection from Hello to Bye — handshake, pairing confirmation, capabilities, ports, heartbeats, peer loss, clock offset, latest-wins input |
| Mesh | `mesh/` | `Mesh`: every connection of one computer — who dials, duplicate resolution, reconnects with backoff, transfer streams with their tokens and keys |
| Transport | `transport/` | `AsioNode`: runs a `Mesh` over one TCP listener, one UDP input socket per session and a timer |

- **Threads.** `AsioNode` owns one I/O thread for every socket and timer. Mesh callbacks run on it.
  Commands from other threads are queued to it in order; those that return a result wait for it.
  The timer is set to the mesh's next deadline instead of ticking, so an idle group costs a few
  wakeups per second per peer (heartbeats).
- **Peer loss.** A silent peer is reported unresponsive after 750 ms, so input to and from it stops
  well within the one-second budget; its session survives a Wi-Fi stall and ends only after 10 s.
- **Backpressure.** A transfer stream refuses messages while 4 MiB are queued for writing and
  reports itself writable again below 1 MiB, so a file never fills memory or delays the control
  connection, which has its own TCP connection.
- **Traffic marking.** On macOS, input sockets are marked as interactive voice and control
  connections as signaling, so Wi-Fi gives them priority. Windows has no unprivileged equivalent;
  sockets stay unmarked there.

## macOS input layer

Built in Phase 1 (`macos/Osdrop/Services/Input`):

- **Capture** — `EventTapCapture` runs an active event tap at the HID level on its own thread. The
  callback translates each event into a platform-neutral `InputEvent` (USB HID usages for keys) and
  enqueues it; in capture mode it also swallows the event. Events Osdrop posts carry a tag in their
  source user data and always pass.
- **Freezing and hiding the cursor** — `CGAssociateMouseAndMouseCursorPosition` and
  `BackgroundCursorHider` (the private `SetsCursorInBackground` property, looked up at run time).
- **Ways out of a capture** — the release chord ⌃⌥⌘⎋ (handled in the tap callback); the in-app
  watchdog, which ends a capture whose owner stopped confirming it for 1 s or whose tap stayed
  disabled; and `OsdropCaptureGuard`, a separate process that ends Osdrop after 2 s without a
  heartbeat, because a hung app cannot undo its own frozen, hidden cursor.
- **Injection** — `CGEventInjector` posts CoreGraphics events with click counts, explicit modifier
  flags and a ledger of held keys and buttons, which `releaseAll` lets go of on every way out.
- **Focus** — `InvisibleKeyWindowFocus` takes keyboard focus during capture so password fields in
  other apps resign and Secure Event Input turns off; `SystemSecureInput` names the app that still
  holds it.

## macOS drag bridge

Built in Phase 2 (`macos/Osdrop/Services/Drag`):

- **Capture at an edge** — `DragActivityMonitor` notices a drag in progress from the drag
  pasteboard's change count while the left button is down. `EdgeDropTarget` is a nearly transparent
  panel along an edge, shown only during drags. It accepts a drop as a copy (the source app keeps
  its files) or refuses it (the drag slides back). File URLs are read at once; file promises are
  received into a staging folder, with a visible wait and a 2-minute limit.
- **Start from injected input** — `PromiseDragStarter` places a small window under a point; the
  injected button press there starts an `NSDraggingSession` with one `NSFilePromiseProvider` per
  item, and injected movement carries it to the drop.
- **Fulfil promises** — `PromisedItemWriter` writes each item into the folder the drop target names,
  under a free name, through a hidden partial file that is moved into place only when complete;
  progress is published for Finder. Content comes from a `PromisedContentReading`: local files now,
  the transfer engine in Phase 14. Items up to 64 MB are also offered as a staged file URL for
  apps that take no promises.
- **Threads** — AppKit runs some promise callbacks on background queues (file coordination for
  sandboxed targets, delivery of received promises). Those callbacks are nonisolated and tested
  from background queues.
