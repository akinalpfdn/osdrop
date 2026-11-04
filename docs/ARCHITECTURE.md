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

Built in Phases 5–9, 13 and 15 (`core/src`). Everything that decides what goes on the wire is sans-I/O:
bytes, datagrams, time and commands go in; bytes, datagrams and callbacks come out. Tests drive
it with a simulated clock and network; one Asio layer drives it in the apps.

| Layer | Directory | Role |
|-------|-----------|------|
| Protocol | `protocol/` | Strict decoders and canonical encoders for every message ([`PROTOCOL.md`](PROTOCOL.md)) |
| Crypto | `crypto/` | Noise XX, cipher states, session and stream keys, input datagram sealing, pairing code |
| Session | `session/` | `SessionEngine`: one control connection from Hello to Bye — handshake, pairing confirmation, capabilities, ports, heartbeats, peer loss, clock offset, latest-wins input |
| Mesh | `mesh/` | `Mesh`: every connection of one computer — who dials, duplicate resolution, reconnects with backoff, transfer streams with their tokens and keys |
| Group | `group/` | `Group`: the mesh's observer — trust from the roster, pairing into a group, roster gossip, revocation, leaving, per-member permissions; passes members' events on to the layers above |
| Layout | `layout/` | `Layout`: next in the chain — screen reports and the layout document between members; the arrangement checks; the crossing engine (below) |
| Transfer | `transfer/` | `TransferService`: after the layout — files and folder trees between members on transfer streams (below) |
| Clipboard | `clipboard/` | `Clipboard`: after the transfers — announcements of copies and the newest copy (below) |
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
- **Group state.** `Group` hands its state (group ID, roster, permissions) to the platform after
  every change as bytes the platform stores in its secure store and passes back at start. The
  format: magic `ODGS`, a version byte (1), the group ID, the pairings (device ID, key, epoch,
  platform, name), the revocations (device ID, epoch) and the permissions that differ from the
  default; counts are u16 and integers little-endian, as on the wire. A newer version is refused,
  so it is never overwritten.
- **Layout state.** `Layout` hands the platform its state after every change: magic `ODLY`, a
  version byte (1), the group ID, the layout document as an encoded LayoutState message and each
  member's last ScreenReport, each embedded with a u16 length so the wire checks apply. The
  platform stores it with its settings; a newer version is refused.
- **Traffic marking.** On macOS, input sockets are marked as interactive voice and control
  connections as signaling, so Wi-Fi gives them priority. Windows has no unprivileged equivalent;
  sockets stay unmarked there.

## Layout and crossing

Built in Phase 9 (`core/src/layout`). Each monitor has a logical frame, where the arrangement
places it, and a native rectangle, where its computer's own cursor moves (macOS points, Windows
physical pixels).

- **Outer edges.** The cursor leaves a computer only through an edge that no other monitor of that
  computer continues, in native units, so a computer's own monitors behave as its system arranges
  them.
- **Neighbours.** The exit point goes to logical units and into the arrangement. The cursor
  continues on the facing monitor of another reachable, attached computer whose edge is within 8
  units of the exit edge and whose span contains the exit point, the nearest first, at the same
  position along the edge and just inside. Anywhere else the edge is a wall. Leaving and coming
  back through the same edge returns within one unit (property-tested).
- **Edge push.** The cursor crosses after the user has pushed against an edge with a neighbour for
  1 s and 32 units, without a pause longer than 300 ms, or at once in immediate mode. These are
  defaults that Phase 12 turns into settings.
- **Virtual cursor.** While the input is on another computer, `CrossingEngine` moves a cursor for
  it in that computer's native units: this computer's movement in logical units, times that
  computer's speed setting, times the scale of the monitor under the cursor. Between that
  computer's monitors it moves as its system would: straight to a point on any monitor, otherwise
  horizontally then vertically through monitors that continue each other. It comes home to where
  it left when its computer drops out of the arrangement.
- **Observer chain.** mesh → `Group` (trust) → `Layout` (arrangement) → `TransferService` (files)
  → `Clipboard` → the platform, so these layers hear only from members.

## Transfers

Built in Phase 13 (`core/src/transfer`). A computer offers items (a dragged file or folder, the
files of a clipboard) to chosen members; a member that wants one opens a transfer stream to it and
fetches it into a folder.

| Part | File | Role |
|------|------|------|
| Names | `names`, `unicode` | NFC from generated Unicode 15.1 tables; the mapping to what the receiving file system can hold; numbering of names one folder would hold twice |
| Files | `files` | UTF-8 paths, `\\?\` long paths on Windows, times in microseconds, free space, ranged reads, a writer that verifies and moves its file into place without replacing anything |
| Manifest | `manifest` | Scanning offered items (parents first, inner links not followed, NFC names), splitting manifests into messages, checking and mapping a received manifest, where a received link may point |
| Streams | `streams` | A queue per stream that holds messages while the mesh refuses more; mesh calls as deferred work |
| Sender | `sender` | Offers, one scan each; requests served in order per stream; reads of 1 MiB at a time, the next only once the stream's queue is below 1 MiB |
| Receiver | `receiver` | One stream per transfer up to both sides' stream limits; manifest, free space and folders first, then contents with eight requests in flight; links last; rollback on any failure |
| File thread | `file_thread` | Runs file work in order, off the I/O thread |

- **Threads.** Reading, hashing, writing and scanning run on the file thread, never on the I/O
  thread that carries input; results come back to the I/O thread as posted work. Tests run the
  same code with the file work queued and drained between simulated steps.
- **Integrity.** Each ContentEnd carries the BLAKE2b-256 digest of the whole entry. The sender
  hashes while it reads and keeps the digest for later requests; before ContentEnd it checks that
  the file still has the size and time it had when scanned (IO_ERROR otherwise). The receiver
  hashes what it writes and moves the file into place only when size and digest match.
- **Nothing left behind.** A receiver writes each file to a hidden temporary (`.osdrop-<id>.part`)
  next to its target and records everything it creates. A failure, a cancellation from either
  side, a lost peer or quitting removes the temporaries and everything the transfer created, in
  reverse order, before the end is reported.
- **Backpressure.** The sender reads only while its stream's queue has room. The receiver stops
  reading its stream while 8 MiB wait to be written and resumes below 2 MiB, so TCP holds the
  sender back instead of the receiver's memory filling.
- **Names.** Names travel in NFC. A receiver maps them for its file system (Windows: fullwidth
  look-alikes for refused characters, look-alikes for trailing dots and spaces, an underscore after
  reserved device names; both: shortening to 255 bytes or UTF-16 units), numbers names a
  case-insensitive folder would hold twice, and numbers top-level names the destination already
  holds (" (2)").
- **Links.** Links inside a folder travel as links whose content is the target, `/`-separated. A
  receiver recreates a link only when its target names another entry of the same item, rewritten
  to the mapped names; other links, and links Windows does not let the user create, are skipped
  and counted. A path offered as an item is followed when it is a link.
- **Permissions.** A receiver fetches only from members whose "files" permission is on, and ends
  their transfers when it is turned off. A sender serves an item only to the members it was
  offered to.
- **Received files are not marked** (no quarantine attribute, no Mark of the Web), by the user's
  decision.

## Shared clipboard

Built in Phase 15. A copy on any member is announced to the members sharing clipboards with it;
the others fetch it in the background and put it on their own clipboard as real data.

- **Core** (`core/src/clipboard`). `Clipboard` sits after the transfers in the observer chain. A
  copy gets a clip ID from the wall clock, above every clip ID seen, so the newest copy wins on
  every member ([`PROTOCOL.md`](PROTOCOL.md), "0x40 ClipboardAnnounce"). Text, RTF, HTML and PNG are
  offered as single contents from memory; files are offered as an item, scanned at once, and the
  copy is announced when the scan gives their size. A newer copy from another member withdraws
  this computer's offers. Receivers fetch formats into memory (`fetch_content`) and files into a
  folder (`receive`), each with a size limit.
- **macOS** (`Features/Clipboard`). `ClipboardController` reads the pasteboard's change count
  every 500 ms. A copy is read only while a member is connected, never when it carries a concealed
  or transient marker (`org.nspasteboard.ConcealedType` and the older password manager types), and
  formats over their limit stay behind; a TIFF-only image is converted to PNG off the main thread.
  Another member's copy is fetched within the limits (text 16 MiB, images 64 MiB, files 512 MiB)
  and written to the pasteboard once everything arrived, PNG and TIFF for images, files as file
  URLs from a staging folder; macOS asks for lazily provided data at once, so nothing is provided
  lazily and no paste waits on the network (DECISIONS.md). What could not be fetched shows a notice
  in the menu.
- **Windows** (`windows/shared` and `windows/engine/src/clipboard`). `ClipboardSync` in the
  portable library does the same as the Mac controller and runs on the clipboard thread of
  `WinClipboard`, which owns a message-only window: `AddClipboardFormatListener` reports copies;
  copies marked with `ExcludeClipboardContentFromMonitorProcessing`, `Clipboard Viewer Ignore`, or
  `CanIncludeInClipboardHistory` / `CanUploadToCloudClipboard` set to 0 are not read. Text travels
  as UTF-8 (CF_UNICODETEXT), HTML without the CF_HTML header, images as PNG (from "PNG", CF_DIBV5
  or CF_DIB through WIC; written as "PNG" and CF_DIB), files as CF_HDROP with "Preferred
  DropEffect" set to copy. The shell shows a notice in the Computers window.

## Group on the platforms

Built in Phase 10. Both apps start core's mesh from their stored identity and states, pass every
Bonjour result to it, advertise this computer's group, and show pairings; core decides whom to
dial and whom to trust.

- **macOS** (`macos/Osdrop`). `GroupController` (main actor) owns a `CoreMesh` actor, so core's
  waiting commands never run on the main thread. `DNSServiceDiscovery` uses the DNS-SD C API:
  Network.framework cannot advertise a port core's socket owns. The identity and group state are
  Keychain items; the arrangement and the listening port are preferences. `SystemEvents` ends
  sessions before sleep and quit and browses again after waking. `PairingWindowPresenter` shows
  the pairing window on whichever Mac takes part.
- **Windows** (`windows/`). `GroupEngine` in `windows/shared` holds the same logic in portable
  C++, tested on every host with fake DNS-SD and real meshes on loopback. The engine DLL adds
  DNS-SD (`DnsServiceRegister`/`Browse`/`Resolve`), DPAPI files under `%APPDATA%\Osdrop`, monitor
  enumeration in physical pixels with logical frames from core, and a C API whose callback hands
  the shell one event struct. The shell calls the engine on one serial worker, never on the UI
  thread, and posts events to the dispatcher.
- **Advertising the group.** When the group changes, the old Bonjour registration is withdrawn
  and a new one made, so browsers see the new `grp` value as a change and pass it on; members
  then dial the computer that just joined.

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
