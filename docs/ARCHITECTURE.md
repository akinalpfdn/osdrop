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
| `windows/input/` | `OsdropInput.exe`: input capture and injection as SYSTEM in the user's session, and `OsdropInputTool.exe` for developers |
| `windows/service/` | `OsdropService`: keeps `OsdropInput` running in the active session |
| `windows/system/` | Win32 pieces shared by the engine, the input process, the service and the tool |
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

Built in Phases 5–9, 11, 13 and 15 (`core/src`). Everything that decides what goes on the wire is sans-I/O:
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
| Control | `control/` | `Control`: last before the platform — the cursor and keyboard across computers, as controller and endpoint (below) |
| Transport | `transport/` | `AsioNode`: runs a `Mesh` over one TCP listener, one UDP input socket per session and a timer |

- **Threads.** `AsioNode` owns one I/O thread for every socket and timer. Mesh callbacks run on it.
  Commands from other threads are queued to it in order; those that return a result wait for it.
  The timer is set to the mesh's next deadline instead of ticking, so an idle group costs a few
  wakeups per second per peer (heartbeats).
- **Peer loss.** A silent peer is reported unresponsive after 750 ms, so input to and from it stops
  well within the one-second budget; its session survives a Wi-Fi stall and ends only after 10 s.
- **Orderly close.** When the mesh ends a connection, its last bytes (a pairing refusal, a Bye)
  go out, then only the sending side is shut and the node reads until the peer closes or 2 s
  pass. Closing a socket with unread bytes resets the connection, and Windows then drops what the
  peer had not read yet.
- **Backpressure.** A transfer stream refuses messages while 4 MiB are queued for writing and
  reports itself writable again below 1 MiB, so a file never fills memory or delays the control
  connection, which has its own TCP connection.
- **Group state.** `Group` hands its state (group ID, roster, permissions) to the platform after
  every change as bytes the platform stores in its secure store and passes back at start. The
  format: magic `ODGS`, a version byte (2), the group ID, the pairings (device ID, key, epoch,
  platform, name), the revocations (device ID, epoch), the permissions that differ from the
  default and, from version 2, the blocks (device ID, version, flags, platform, name); counts are
  u16 and integers little-endian, as on the wire. Version 1 states are still read; a newer
  version is refused, so it is never overwritten.
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
  → `Clipboard` → `Control` (input) → the platform, so these layers hear only from members.

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

## Cursor and keyboard across computers

Built in Phase 11 for `core` and the Mac; on Windows, both halves came with Phase 3 (see
"Windows input process"). Core decides every crossing, refusal and release; the platforms capture,
inject, hide the cursor and show state.

- **Core** (`core/src/control`). `Control` is both parts of every computer. As a controller it
  takes all of the computer's physical input from the platform and drives the crossing engine with
  it, among the members that are connected, answering and accepting control. When the cursor
  crosses, it tells the platform to capture (with the keys held at that moment, which the
  platform releases locally), sends Enter with the held modifiers, then motion as datagrams and
  keys, buttons and scrolls with the cursor's position on the control connection. Crossing on or
  back sends Leave. No crossing happens while a button is held, unless it carries a drag of files,
  or while input is locked ("lock input here"). The cursor comes home to where it left on the user's hotkey, a capture the platform
  lost, a Refuse, a LocalTakeover, or a computer that stops answering for 750 ms, leaves, or stops
  accepting control. As an endpoint it accepts one controller at a time (others get Refuse busy,
  as does a computer whose own input is away), refuses while the platform reports the screen
  locked or injection not permitted, and hands each event to the platform to inject. Its ledger
  of injected keys and buttons is released on Leave, ReleaseAll, local takeover, a controller that
  stops answering or leaves, a permission withdrawn and the screen locking. Physical input on a
  controlled computer ends the control at once (local input wins). It reports its cursor when an
  application moves it, and measures motion latency through the session clock offset (median and
  99th percentile of the last 1000 datagrams).
- **macOS** (`Features/Control`). `InputController` runs an event tap and an injector of its own
  (the developer tools keep theirs). The tap's events go to core from a detached task, so a busy
  main thread never delays the cursor elsewhere; click counts come from the Mac's double-click
  settings. Core's events are applied on the main actor, which also sends the capture heartbeat, so
  a hung main thread ends the capture. The lock chord ⌃⌥⌘L is swallowed in the tap in both modes;
  ⌃⌥⌘⎋ remains the way out of a capture. Without Accessibility the Mac does not watch input and
  refuses control (not permitted); a locked screen refuses too. The menu bar icon changes and the
  menu names the controller while another computer controls the Mac.
- **Windows** (`windows/engine/src/input`, through the input process). The engine's
  `InputBridge` has OsdropInput watch the PC's input while a member is connected, and the
  `InputForwarder` turns its reports into core's input: positions kept on a monitor as Windows
  keeps them while the push still moves, click counts from the PC's double-click settings by where
  the cursor is on the other computer, repeats for keys that go down again. On a crossing the
  bridge parks the hidden cursor in the middle of its monitor, releases the held keys locally and
  confirms the capture every 250 ms; on the way back it sets Caps Lock as the user left it and
  shows the cursor where core says. Ctrl+Alt+Win+Esc and Ctrl+Alt+Win+K are the chords; the
  indicator and notices come with the Windows interface (Phase 17).
- **Fidelity** (Phase 12). Keys travel as HID usages and the computer they arrive on applies its
  own layout; Control and Command/Windows swap between a Mac and a PC unless the user chose keys
  by position; Caps Lock travels as a state; scrolling travels as content movement, a line being a
  notch everywhere. The Mac's table is `MacKeyMap`; the PC's, scan codes for the Phase 3 input
  process, is `key_codes` in `windows/shared`, with `wheel` for scrolling. The crossing settings
  and pointer speeds come from preferences (`input.` keys) until the Phase 16 settings window.

## Drag and drop across computers

Built in Phase 14 for `core` and the Mac; the PC's drag bridge comes with Phase 4. The protocol
rules are in PROTOCOL.md, "Drag messages".

- **Core** (`control/control_drag.cpp`, part of `Control`). As a source it offers each item of a
  drag on this computer to the transfers when the platform reports the drag, and answers probes
  from its controller once the items are measured. As a controller, a push against an edge with
  only the left button held asks the computer under the cursor for its drag (or looks at its own);
  a drag lets the push cross toward computers that take drags, and the controller forwards the
  offer before the Enter that names it. As a target it reports the drag to the platform and holds
  the left button for it in its ledger. The source keeps its button down while the drag is away,
  until DragResult; every way control ends either carries the drag on (Leave) or cancels it, so a
  drag never stays on a computer that no longer has the cursor. Dropped items stay offered until
  16 newer drags have been dropped.
- **macOS** (`Features/Drag`, `Services/Drag`). `DragController` gets core's drag events from the
  input controller, in order with the input around them. Drags on this Mac are noticed on the drag
  pasteboard and reported with their file URLs. A drag of promised files (Photos, Mail
  attachments) is reported as such; when another computer wants it, the Mac drops it on a target
  at the cursor, receives the files into staging and reports them, and the drag goes on as files.
  A drag that arrives starts from Phase 2's `PromiseDragStarter`; its promises are fulfilled by
  `NetworkContentReader`, which receives each item beside the drop's destination and moves it
  there whole, waiting through CoreBridge's `TransferWatch`, which follows transfers on core's
  thread so a staged copy asked for on the main thread cannot wait for it. A drag that moves on is
  cancelled with Escape. This Mac's own drag that waited at the edge ends over a small drop target
  at the cursor that takes it as a copy or refuses it.
- **Progress** — the receiving side follows each transfer by its ID; the sending side gets
  `SendProgress` from core's sender for every member fetching an item, until every byte went out or
  the member stopped. The menu lists both directions with their progress and a cancel button
  (cancelling a send withdraws the drag); a notification tells when a transfer ends or fails.
  Finder also shows the receiving copy's progress on the file.

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

## macOS interface

Built in Phase 16 (`macos/Osdrop/Features/Settings`, `Onboarding`, `Updates`, `App/MenuContentView`),
following Osplay's macOS app so the sibling products look and behave alike: system fonts, grouped
forms, SF Symbols, every visual value from `Theme`, every string from the catalog in English and
Turkish.

- **Menu bar panel.** Where the mouse and keyboard are, the lock and clipboard sharing as quick
  toggles, an update when one is out, the group, and Settings and Quit as icons. Another icon
  shows while another computer controls the Mac.
- **Settings.** `AppSettings` keeps every user setting in UserDefaults and saves each change at
  once; `SettingsViewModel` hands input settings, pointer speeds and chords to the running
  `InputController`, permissions and the arrangement to the group, and the name to a restart of
  the mesh. The arrangement editor draws each member's monitors to scale; a dropped computer
  snaps beside the nearest other one, keeping an edge in common, and core checks the result.
  General has launch at login (`SMAppService`) and the daily update check against GitHub
  releases; Advanced copies diagnostics without names, addresses or contents and exports the log.
- **Chords.** The return and lock chords are settings; the event tap matches them in both modes.
- **Onboarding.** At first launch, and whenever Accessibility is missing: Accessibility and Local
  Network with their live state and links to System Settings.
- **Errors.** `ErrorMessages` gives every core error a localized message that says what to do.

## Windows interface

Built in Phase 17 (`windows/app/Osdrop.App`), following Osplay's Windows shell and the Mac app
above, so both apps work the same way: WPF-UI's Fluent controls following the system's light or
dark theme live, every visual value from `Theme/Tokens.xaml`, every string from
`Osdrop.Localization` in English and Turkish.

- **Tray icon and panel.** The icon is the Mac menu bar glyph, dark or light to match the taskbar,
  filled while another computer controls this PC (`TrayIconTheme`). A click opens the panel above
  the taskbar, the Mac menu's counterpart: where the mouse and keyboard are, the lock and
  clipboard sharing, copied files on their way, the group and nearby computers, and Settings and
  Quit as icons. Problems come first: an engine that did not start, the input service, an update.
  A second start of Osdrop opens the running one's panel (`SingleInstance`).
- **Settings.** The Mac's tabs: General (start with Windows through the Run key, daily update
  check), Arrangement (the same editor and snapping, `ArrangementGeometry`), Devices (name,
  permissions, remove, block, unblock, pair), Input (crossing, modifier mapping, pointer speeds,
  the two chords with a recorder; the engine checks them as the hook matches them and sends them
  to OsdropInput after every reconnect; no crossing while a game holds the cursor, below),
  Clipboard and Files, Advanced (diagnostics without
  names or addresses, the shell's and the input service's log folders). `AppSettings` saves every
  change to `%APPDATA%\Osdrop\settings.json` at once and the engine gets it at once; a new name
  restarts the engine, as the Mac restarts its mesh.
- **Engine calls.** `Native.Run` puts every call on one queue of thread-pool threads, so the UI
  thread never waits for native code and calls never overlap; the engine's callback posts events
  to the dispatcher. The engine reports the input state (`osdrop_engine_get_input_state`: whether
  OsdropInput answers, the lock, who controls this PC, where the cursor is, why it came back)
  and an INPUT_CHANGED event when it changes; LAYOUT_CHANGED when the arrangement does.
- **Input service.** While OsdropInput does not answer, the panel says whether the service is
  missing, stopped or not answering (from the service manager) and offers to repair it:
  `OsdropService.exe --install` from Osdrop's folder, elevated.
- **Games.** While an application holds the cursor in a rectangle smaller than the desktop
  (ClipCursor, as full-screen games do), the bridge gives the rectangle to the `InputForwarder`,
  which keeps positions in it and reports only the movement inside it: a push at its edge moves
  nothing, so core never crosses. The bridge reads the rectangle every 100 ms while watching.
- **Errors.** `LocalizedText.Error` gives every core error a localized message that says what to do.

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
  progress is published for Finder. Content comes from a `PromisedContentReading`: local files in
  the developer loopback, the transfer engine for drags from other computers. Items up to 64 MB are also offered as a staged file URL for
  apps that take no promises.
- **Threads** — AppKit runs some promise callbacks on background queues (file coordination for
  sandboxed targets, delivery of received promises). Those callbacks are nonisolated and tested
  from background queues.

## Windows drag bridge

Built in Phase 4 (`windows/engine/src/drag`, portable parts in `windows/shared` `drag_files`):

- **Capture at an edge** — `EdgeTargets` keeps nearly transparent, topmost windows that never take
  the focus along the monitors' outer edges (`edge_strips`), shown while the left button is down.
  A drop is taken as a copy (the source keeps its files): CF_HDROP gives the paths; virtual files
  (Outlook attachments, zip folders) are saved into `%TEMP%\Osdrop\drags` first, on a thread of
  their own when the source supports IDataObjectAsyncCapability. Names that would leave the
  staging folder are refused. Elevated sources cannot reach the targets (UIPI).
- **Start from injected input** — `start_drag` shows a tiny window under a point on an STA
  thread; the left button pressed there starts DoDragDrop, and the moves that follow carry the
  drag. The data object offers a FILEGROUPDESCRIPTORW for every file and folder
  (`virtual_files`: paths a descriptor can hold, parents first) and one stream per file
  (`FileStream` over a `ContentSource`: reading front to back, seeking forward by reading, back
  by starting over), so Explorer copies with its own progress. It supports
  IDataObjectAsyncCapability and keeps its thread pumping until the target finished reading.
  Drags up to 64 MB are also offered as CF_HDROP to copies staged on request. The drag image is
  the first item's shell icon.
- **Loopback** — a developer tool in Debug builds: a drag dropped at an edge comes back at the
  left edge of the same monitor as an Osdrop drag of local files.

## Windows input process

Built in Phase 3, with the PC's controller half of Phase 11. Hooking and injecting over elevated
windows needs a process above their integrity level, so input runs in a SYSTEM process inside the
user's session, apart from the engine (security reasoning: `SECURITY_MODEL.md`, "Windows input
process").

- **OsdropService** (`windows/service`). A LocalSystem service whose only job is to keep
  `OsdropInput` running in the active console session: started there with the service's own token
  (its session changed) on `winsta0\default`, moved on log on, log off and fast user switching,
  asked to quit through `Global\com.akinalpfdn.osdrop.input.stop.<session>` before it is ended,
  restarted at once after a run of a minute or more and with pauses from 0.5 to 30 s while it keeps
  failing. `--install`, `--stop` and `--uninstall` are for the installer and for development.
- **OsdropInput** (`windows/input`). Per-monitor DPI aware v2, so every position is a physical
  pixel of the virtual desktop. Low-level hooks run on a time-critical thread with its own message
  loop; their callbacks only decide and enqueue (`ReportQueue`: motion added up while the pipe is
  busy, keys and buttons never dropped) and are timed (`HookTimings`), because Windows removes a
  hook whose callback is slow. Its own SendInput events carry the tag `OSDP` in `dwExtraInfo` and
  pass the hooks unreported. Movement is measured from where the cursor is when the hook runs to
  where Windows takes it, before Windows keeps it on a monitor, so a push against an outer edge
  still moves; for 2 ms after OsdropInput moves the cursor itself, movement is not reported. While
  watched, this PC's input acts here and is reported; while captured, it is swallowed and reported,
  the cursor waits hidden at the park point, every system cursor is replaced by a transparent one,
  and the saved ones come back on release. The capture ends inside OsdropInput, with the cursor
  where it left, when its heartbeat stops for a second or the user presses Ctrl+Alt+Win+Esc; the
  hook matches the chords (`chords`) while it watches or captures, and at home presses an
  unassigned key after one so releasing Win or Alt opens no menu. A marker file in `%ProgramData%\Osdrop` says the cursors are
  hidden: after a crash the next `OsdropInput` reloads them (as SYSTEM that is the default
  scheme), and the client, which runs as the user, reloads the user's own scheme when the pipe
  breaks. While controlled, this PC's own input is reported as LocalInput, at most every 100 ms.
- **Input pipe** (`windows/shared/input_protocol`, portable and tested on every host). A
  message-mode pipe per session, one message per pipe message, little-endian: magic `ODPI`,
  version u16 (1), type u16, body length u32, body. Requests: BeginControl, EndControl, Move
  (x, y i32 pixels), Press (button u8 0-4, down u8), Scroll (vertical, horizontal i32 in
  WHEEL_DELTA units), Key (scan code u16, extended u8, down u8), StartCapture (park x, y, home
  x, y), StopCapture (x, y), QueryStats, Watch (on u8), Heartbeat, SetChords (return, lock: each
  modifiers u8, scan code u16, extended u8; the defaults until a client sends them and after it
  goes). Reports: CapturedMotion (dx,
  dy i32, ticks u64), PhysicalPress, PhysicalScroll, PhysicalKey, LocalInput, Stats (events u64,
  p50, p99, max u32 in µs), Rejected (type u16, reason u16: 1 malformed, 2 another version,
  3 injection without control or capture), WatchedMotion (x, y, dx, dy i32, ticks u64),
  HotkeyPressed (u8: 1 return, 2 lock) and Released (u8: 1 return chord, 2 no heartbeat). Ticks
  are QueryPerformanceCounter values, so the engine knows how old an input is. Positions are at
  most 2^20 either way, scrolls 64 notches, and keys only scan codes in the key table.
  `InputSession` applies them: injection while the client controls this PC or captures its input
  (keys released here, Caps Lock), what the client pressed is released when control or capture
  ends or the client goes, and a capture without a Heartbeat for a second ends here. Absolute
  moves use SendInput's 0-65535 scale over the virtual desktop, rounded so every pixel is reached
  exactly (`absolute`).
- **Engine** (`windows/engine/src/input`). `InputBridge` connects to the pipe (again every 2 s
  while it cannot) and tells core this PC cannot be controlled while it is unreachable. Its thread
  is per-monitor DPI aware, so it reads and writes physical pixels as OsdropInput does. Core's
  callbacks only queue; the bridge's thread turns each injected input into requests (`Injection`:
  positions from 1/16 pixel, buttons and scrolls moving the cursor first, keys by scan code, Caps
  Lock pressed only when it differs) and keeps the newest of a run of moves; it starts and stops
  captures, sends heartbeats, and reads Caps Lock every 250 ms while the cursor is home. Reports go
  from the pipe's reader thread straight to core: watched and captured input through the
  `InputForwarder`, LocalInput as physical input that gives the control back to this PC's user,
  the chords and Released as core's return and lock. A cursor that something else moved during
  control, holding for two 100 ms checks, is reported. A pipe that breaks during a capture brings
  the cursor home.
- **OsdropInputTool** (`windows/input/tool`). `capture [s]` captures this PC's input and plays it
  back at the same pace; `stats` prints the hook timings. It is the hardware check of this phase
  and runs only next to `OsdropInput`.
