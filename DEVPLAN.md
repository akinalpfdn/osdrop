# Osdrop — Development Plan

## Overview

Osdrop lets one keyboard and mouse move across several computers on the same local network. Push the cursor through a screen edge and it continues on the neighbouring computer; the keyboard follows it. Drag a file to that edge, keep pushing, and the file crosses with the cursor; release the button and the file is copied exactly where it was dropped. Copy on one computer, paste on another.

It works in every direction between macOS and Windows: Mac ↔ PC, Mac ↔ Mac and PC ↔ PC, with any number of computers in one arrangement, and whichever computer's mouse and keyboard are in use drive the others. Apple's Universal Control does this between Apple devices on one Apple ID; Osdrop is for mixed desks.

It is not screen sharing (that is Osplay, the sibling project) and not remote desktop over the internet. No pixels are sent; each computer keeps its own monitors.

The project is open source (MIT), distributed as a notarized macOS app and a Windows installer, with no backend services of any kind.

**Production grade from the first phase.** There are no throwaway prototypes. Platform risks are retired by early phases that build the real production modules and end with a go/no-go acceptance gate (user decision 2026-11-02).

---

## Platform & Stack

### Repository layout (monorepo)

- `core/` — shared C++20 library: protocol codec, crypto, transport, session engine, group membership, layout and crossing engine, input event model, clipboard model, file transfer engine. Exposes a flat C API. Used by both apps, so every rule that both sides must agree on (wire format, crossings, roster merges, file name mapping) has exactly one implementation.
- `macos/` — Swift menu bar app; consumes `core` through SwiftPM (`CoreBridge`).
- `windows/engine/` — C++20 DLL loaded by the shell: sessions on top of `core`, discovery, key storage, drag and drop, clipboard, file I/O, pipe client to the input process.
- `windows/input/` — C++20 `OsdropInput.exe`: low-level hooks, cursor parking, input injection. Runs as SYSTEM inside the user's session so it works over elevated windows.
- `windows/service/` — C++20 `OsdropService`: a LocalSystem service whose only job is to keep `OsdropInput` running in the active console session.
- `windows/shared/` — Windows-header-free libraries (pipe protocol, request validation, pure mappings) that build and test on macOS too.
- `windows/app/` — C# WPF shell (tray panel, settings, pairing, arrangement). Calls the engine via P/Invoke.
- `windows/installer/` — Inno Setup script.
- `protocol/` — fixtures and the independent fixture generator.
- `docs/` — architecture, protocol, building, testing, troubleshooting, security model, third-party notices, releasing.
- `.github/` — CI, release workflow, issue/PR templates, Dependabot.

### macOS

- Target: macOS 14 Sonoma and later. Apple Silicon is supported and tested; Intel builds are produced but marked untested.
- Language: Swift 6 with strict concurrency checking.
- UI: SwiftUI (`MenuBarExtra` with `.window` style, `Settings` scene, onboarding window). AppKit where SwiftUI cannot do the job: edge drop-target panels, drag sessions, the arrangement editor's drag interaction if SwiftUI gestures prove insufficient.
- Architecture: MVVM with `@Observable` view models. Sessions and the input controller are actors; event tap callbacks run on a dedicated thread and only enqueue. Platform services sit behind protocols and are injected.
- Project generation: XcodeGen (`project.yml`).
- Distribution: Developer ID, Hardened Runtime, notarization, no App Sandbox (Accessibility-driven input injection is not available to sandboxed apps).
- System frameworks: CoreGraphics (event taps, event posting, display geometry), AppKit (drag sessions, pasteboard, file promises), DNS-SD C API (Bonjour, per Osplay's decision), Security (Keychain), ServiceManagement (login item), UniformTypeIdentifiers.

### Windows

- Target: Windows 10 22H2 and Windows 11. x64 is supported and tested; ARM64 builds are produced but marked untested (Osdrop needs no driver, so ARM64 is possible from the start).
- Engine, input process, service: C++20, CMake presets (Ninja + MSVC from a developer shell, `portable` preset for the Windows-header-free libraries), `/W4 /WX`, COM via `Microsoft::WRL::ComPtr`. Every process is per-monitor DPI aware (v2).
- Shell: C# on .NET 10 (LTS), WPF, nullable reference types, warnings as errors, MVVM with CommunityToolkit.Mvvm.
- Key dependencies: WPF-UI (Fluent controls, system theme), H.NotifyIcon.Wpf (tray), CommunityToolkit.Mvvm, Inno Setup (installer).
- System APIs: low-level hooks (`WH_MOUSE_LL`, `WH_KEYBOARD_LL`), `SendInput`, OLE drag and drop (`RegisterDragDrop`, `DoDragDrop`, `IDataObjectAsyncCapability`, `IDragSourceHelper`), clipboard (`AddClipboardFormatListener`, delayed rendering), WTS session APIs, DNS-SD (`DnsServiceRegister` / `DnsServiceBrowse`), DPAPI, `BCryptGenRandom`.

### Shared core

- C++20, CMake, also exposed to SwiftPM.
- Dependencies vendored under `core/third_party/` at pinned versions with licenses: standalone Asio, Monocypher 4. GoogleTest for tests only via FetchContent.
- **Code from Osplay is copied, not linked** (user decision 2026-11-02): the Noise handshake, cipher states, replay window, randomness injection, Asio transport base, control framing and utilities are copied from Osplay at one recorded commit, adapted (names, labels, prologue) and owned by Osdrop from then on. `DECISIONS.md` records the source commit and `docs/THIRD_PARTY.md` the provenance. A security fix in either project is checked against the other.
- No platform headers in `core` except through Asio. No logging library: `core` logs through a callback the host app installs.

### Cross-cutting decisions

- **Identifier prefix:** `com.akinalpfdn.osdrop` (bundle ID, Keychain service, logging subsystem, pipe and service names derive from it). Bonjour service type `_osdrop._tcp`.
- **Localization: active, English and Turkish from day one.** `Localizable.xcstrings` on macOS, `.resx` in a separate localization assembly on Windows (Osplay lesson: WPF markup cannot see resx accessors generated in the same project). `core`, the engine and the input process return stable error codes, never text.
- **Theming: system-following light and dark on both platforms.** All colors, typography, spacing, radii and elevation come from one theme source per platform; views and XAML consume tokens only, enforced by a lint script.
- **License: MIT.**
- **Group size:** up to 8 computers in one group (a full mesh of at most 28 sessions). The limit is one constant in `core`.

---

## Data Model

All persistent data is local to each computer. Nothing leaves the LAN.

- **Identity** — one static X25519 key pair per installation. The device ID is a fingerprint of the public key. Stored in the Keychain (macOS) or a DPAPI-protected file (Windows). Never synced, never logged.
- **Device** — device ID, public key, display name, platform, pairing epoch, last seen. One record per known peer.
- **Group roster** — the group ID (random, created when the first two devices pair) and the member set. Modelled as a two-phase set keyed by (device ID, pairing epoch): additions and revocations are both grow-only, so any two rosters merge by union and every member converges regardless of message order or who was offline. A revocation can never be undone by a stale member; a revoked device can only come back by pairing again, which creates a new epoch. Stored with the identity (Keychain / DPAPI), because it decides who may connect.
- **Local permissions per device** — whether this computer accepts control, files and clipboard from that device. Decided and stored by each computer for itself, never synced; enforced by `core` on the receiving side.
- **Screen report** — each computer's own monitors: frame in points (macOS) or DIPs (Windows) in its own desktop space, pixel size, scale, a stable monitor key. Published by that computer only; the arrangement never edits a computer's own monitor layout.
- **Layout document** — the placement of each computer's desktop in one shared global space (an offset per device), with a revision number and author device ID. Replaced as a whole; last writer wins by (revision, device ID). Synced to every member.
- **Settings** — crossing behaviour, modifier mapping, per-device pointer speed, hotkeys, clipboard and file options, launch at login. `UserDefaults` on macOS; a JSON file under `%APPDATA%\Osdrop` on Windows.
- **Transfers** — in memory only (progress, state); a short history of finished transfers is kept for the UI and cleared on quit.
- **Schema evolution:** every persisted structure carries a version; loading runs explicit, tested migration steps; unknown newer versions are refused with an error code rather than overwritten.

---

## Constraints & Platform Considerations

### macOS

- **Accessibility permission** is required for an active event tap (which can swallow events) and for posting events. Onboarding detects it, explains why, deep-links to System Settings and watches for revocation at runtime. TCC grants are bound to the code signature, so debug builds are signed with the same Developer ID identity as releases (Osplay lesson) and the identity never changes.
- **Event tap safety:** macOS disables a tap whose callback is too slow (`kCGEventTapDisabledByTimeout`) and by user input (`…ByUserInput`). The callback only enqueues; the controller re-enables a disabled tap at once. If Osdrop hangs, the system timeout gives the user their input back — this is a feature, verified in Phase 1.
- **A hung Osdrop must not keep the mouse:** the frozen, hidden cursor belongs to Osdrop's window server connection, so if the whole app hangs, nothing inside it can undo it (measured in Phase 1). `OsdropCaptureGuard`, a small tool in `Contents/MacOS` started by the app, receives a heartbeat while capture is on and ends Osdrop after 2 seconds of silence; macOS then restores the cursor at once. A hung main thread is handled first, inside the app, by the capture watchdog (1 second). Capture never starts without the guard (user decision 2026-11-02).
- **App Nap:** while the event tap runs, the app holds a latency-critical activity (`ProcessInfo.beginActivity`); App Nap would otherwise delay the watchdog and the guard's heartbeats by minutes (measured in Phase 1) and add latency to every event.
- **Cursor while the input is elsewhere:** `CGAssociateMouseAndMouseCursorPosition(false)` freezes the cursor while deltas keep arriving in the tap. Hiding the cursor from a background app is not possible through public API; the established workaround is the private `SetsCursorInBackground` connection property. If needed, it is declared in one Objective-C header and used only behind a `CursorHiding` protocol, like Osplay's `CGVirtualDisplay`.
- **Secure Event Input:** while any app has secure input enabled (a focused password field, Terminal's Secure Keyboard Entry), event taps do not see keystrokes. When the input leaves the Mac, Osdrop asks to make its own invisible window key so the local password field resigns. macOS 14+ activates cooperatively and refused this while Finder was frontmost (measured in Phase 1), so the request is best effort: the UI tells the user that secure input is on and names the app only when macOS's record matches the frontmost app (the record was stale in Phase 1). Keystrokes are never typed locally either way; the tap swallows what it sees.
- **Injected events** are tagged through `kCGEventSourceUserData` so Osdrop can tell them from physical input. Clicks carry the click state field, otherwise double clicks are not recognised. Media keys are posted as system-defined events.
- **Drag capture:** a drag in progress is detected through the drag pasteboard's change count while a button is held. Edge drop-target panels (non-activating `NSPanel`, nearly transparent, only along crossing edges and only while a drag is in progress) receive the drag. A fully transparent window is not a drop target, so the panels keep a minimal alpha.
- **Drag start on the receiving Mac:** `beginDraggingSession` needs a real mouse-down event, so Osdrop injects a button-down into its own helper window under the cursor and starts the session from that event; subsequent injected moves drive the drag and the injected button-up drops it.
- **File promises:** `NSFilePromiseProvider` writes each item into the folder the drop target names, asynchronously. Osdrop resolves name collisions itself (Finder does not for promises) and publishes progress for the destination file so Finder shows it on the icon. Apps that accept only file URLs get a lazily provided URL to a staged copy, below a size cap.
- **Files and Folders privacy:** reading a file that the user dragged onto Osdrop is allowed; reading file URLs from the general pasteboard (clipboard file copy) in Desktop, Documents or Downloads may trigger a TCC prompt. Phase 15 verifies and handles it in onboarding.
- **Received files** get the quarantine attribute, as AirDrop does, unless the user decides otherwise in Phase 13.
- **Local Network permission** (macOS 15+): `NSLocalNetworkUsageDescription` and `NSBonjourServices` in Info.plist; permission status read from a Bonjour browse, as Osplay does.
- **Screen lock and sleep:** a locked or sleeping Mac refuses control; the controller treats it as a wall and shows why.
- **Info.plist:** `LSUIElement = true`. Launch at login through `SMAppService`.

### Windows

- **UIPI:** a medium-integrity process cannot inject into, or hook input destined for, elevated windows (Task Manager, admin terminals, installers). `uiAccess` would avoid this but requires an Authenticode-signed binary, and the project has no certificate. So `OsdropService` (LocalSystem) launches `OsdropInput` with a SYSTEM token into the active console session on `winsta0\default`; it relaunches it on session change, logon, logoff, fast user switching and crashes.
- **Privilege split:** only input capture and injection run as SYSTEM. Networking, protocol parsing, file I/O, clipboard and drag and drop run in the user's medium-integrity process, so a protocol bug never executes as SYSTEM, files are read and written with the user's rights, and OLE drag and drop works (UIPI blocks drops from medium-integrity apps onto higher-integrity windows).
- **Pipe between the app and the input process:** one per session, ACL limited to the session user's SID and SYSTEM, client verified by process image path against the installed app, a strict versioned command set with bounds-checked arguments, and input injection accepted only while a control session is active. The residual risk (malware running as the user could try to drive elevated windows through it) is documented in `docs/SECURITY_MODEL.md` and revisited when code signing becomes available.
- **Low-level hooks:** callbacks must return within `LowLevelHooksTimeout`, or Windows silently removes the hook. Hook callbacks run on a dedicated thread with its own message loop and only enqueue. Injected events are recognised by `LLMHF_INJECTED` and Osdrop's own `dwExtraInfo` tag.
- **Cursor while the input is elsewhere:** the cursor is parked at a fixed point and deltas are computed from the hook positions (pointer ballistics stay the user's own). The cursor is hidden by setting transparent system cursors; they are restored on return, on exit, and at the next start after a crash (`SPI_SETCURSORS`). A visible cursor must never be lost permanently.
- **Injection:** `SendInput` with absolute virtual-desktop coordinates for moves, high-resolution wheel deltas, scan codes (with the extended-key flag) for keys so the target's layout produces characters. Ctrl+Alt+Del and other secure attention sequences cannot be injected; this is documented.
- **Lock screen, UAC prompts, sign-in screen:** these run on the secure desktop. Supporting them is post-MVP (user decision 2026-11-02); until then a locked PC refuses control and the controller treats it as a wall.
- **Drag capture:** a topmost, layered, non-activating window registered with `RegisterDragDrop` on an STA thread, shown only while the left button is held near a crossing edge. It reads `CF_HDROP` and virtual files (`CFSTR_FILEDESCRIPTORW` + `CFSTR_FILECONTENTS`, e.g. Outlook attachments, zip contents). Drags from elevated apps cannot reach it (UIPI); documented.
- **Drag start on the receiving PC:** a helper window receives an injected button-down and calls `DoDragDrop` on a dedicated STA thread with a data object that offers virtual files and implements `IDataObjectAsyncCapability`, so Explorer reads the streams on a background thread with its own progress dialog. A delayed `CF_HDROP` to staged copies serves apps that need real paths, below a size cap.
- **Clipboard:** delayed rendering (`WM_RENDERFORMAT`, `WM_RENDERALLFORMATS`); clipboard history and cloud clipboard may request delayed formats eagerly, verified in Phase 15. Sensitive content is respected through `ExcludeClipboardContentFromMonitorProcessing`, `CanIncludeInClipboardHistory` and `CanUploadToCloudClipboard`.
- **File names:** long paths through `\\?\`, reserved names (`CON`, `NUL`, `COM1`…), invalid characters, trailing dots and spaces are mapped by one tested function in `core`.
- **Received files** get Mark of the Web unless the user decides otherwise in Phase 13.
- **Firewall:** the installer adds one inbound rule for the app on every network profile (Osplay lesson 2026-10-26: Windows 11 marks new networks Public).
- **Code signing:** none for now; SmartScreen warns once. Keyboard hooks combined with input injection in unsigned binaries may be flagged by antivirus products; see Risks.

### Network

- Same LAN only, no relay, no internet requirement.
- Listener ports are ephemeral and advertised via Bonjour; a computer keeps its port across restarts of its listener when it can (Osplay lesson).
- Wi-Fi is a first-class setup. macOS AWDL causes short loss bursts on Wi-Fi (Osplay finding); pointer motion therefore travels as latest-wins datagrams that tolerate loss, and input sockets are marked as real-time traffic.

---

## Architecture

### Roles and topology

Every installation is the same app. Paired computers form one **group** with a shared arrangement. Online members keep a **full mesh**: one encrypted session per pair, dialed by the member with the lower device ID.

At any moment one computer is the **controller**: the one whose physical mouse or keyboard is in use. It owns a virtual cursor in the global arrangement and decides, through the `core` crossing engine, which computer receives input. The others are **endpoints**. The controller is also an endpoint when the cursor is on its own screens; then input is simply not captured.

Rules that keep this safe:
- **Local input always wins.** Physical input on a computer that is being controlled ends the remote control at once and returns the cursor to its controller.
- **Never leave the user without a mouse.** If the controlled computer stops answering, the controller releases capture within one second and the cursor reappears at the point where it left.
- **Everything pressed is released.** An endpoint tracks every key and button it has injected and releases all of them when control leaves or the session ends.

### Data flow

```
CONTROLLER (physical input)                                ENDPOINT (receives input)
Event tap / LL hooks ─► capture ─► core: crossing engine ─┬─UDP motion (latest wins)──► core ─► injector (CGEventPost / OsdropInput SendInput)
                                    (virtual cursor,       └─TCP keys, buttons, scroll─►
                                     edge push, walls)          (with cursor position)
                                                         ◄──TCP cursor report, refusals, local-input takeover

DRAG:   source edge panel captures drag ─► offer (items metadata) ─► target starts promise drag from injected button-down
        drop on target ─► promise / IStream reads ─► transfer stream (TCP, ranged, encrypted) ◄─ source file reader
        drop result ─► source edge panel accepts as copy (or rejects: drag slides back)

CLIPBOARD: copy anywhere ─► announce formats to the group ─► peers install lazy proxies ─► paste ─► fetch on demand
```

### Layers

**core (C++, shared)**
- `protocol` — message and datagram definitions, encode/decode, version negotiation. Pure, no I/O.
- `crypto` — Noise XX, cipher states, replay window, key derivation for input datagrams and transfer streams, pairing code. Copied from Osplay, adapted.
- `transport` — Asio TCP control connections, UDP input socket, TCP transfer streams; one I/O thread owned by `core`.
- `session` — sans-I/O session engine (handshake, pairing confirmation, heartbeat, clock offset, stats) under the thin Asio transport, as in Osplay.
- `mesh` — one session per pair, dial direction, duplicate resolution, reconnect with backoff.
- `group` — roster two-phase set, introductions, revocation propagation, local permission enforcement.
- `layout` — screen reports, layout document and merges, global geometry, crossing detection, entry mapping, edge push state machine, virtual cursor, conversion to each platform's injection coordinates.
- `input` — platform-neutral input events: USB HID usages for keys, buttons, scroll in content-movement semantics with phases, modifier state, the held-key ledger.
- `clipboard` — format model, announcements, sensitivity flags, size caps.
- `transfer` — manifests, ranged reads, integrity hashes, cancellation, portable file name mapping.
- `capi` — flat C API: opaque handles, POD structs, callbacks with a context pointer, error codes. The only public header.

`core` knows nothing about event taps, hooks, windows or pasteboards. Platforms push captured input and file bytes in, and receive commands (inject, start drag, provide clipboard data) out.

**macOS app**
- `App` — SwiftUI scenes: menu bar panel, settings, onboarding, pairing sheet, arrangement editor.
- `Features/Control` — `InputController` actor: capture state, crossings, local takeover, safety timers.
- `Features/Endpoint` — injection, refusal rules (locked, asleep, permission), cursor reports.
- `Features/Drag` — edge panels, drag capture, promise drag sessions, staging.
- `Features/Clipboard` — pasteboard watcher, lazy proxies.
- `Features/Transfers` — progress, history, notifications.
- `Features/Pairing` — pairing flow, group and device management.
- `Services` — protocol-backed adapters: `InputCapturing`, `InputInjecting`, `CursorHiding`, `ScreenDescribing`, `DragCapturing`, `DragStarting`, `PasteboardAccessing`, `FileAccessing`, `PeerDiscovering`, `KeyStoring`, `PermissionChecking`, `HotkeyRegistering`, `LockStateObserving`.
- `CoreBridge` — Swift wrapper over the C API, a target of the root Swift package (Osplay decision).
- `Theme`, `Localization`, `Logging` (os `Logger`, one subsystem, per-module categories).

**Windows engine (C++ DLL, medium integrity)**
- `session` — orchestration on top of `core`.
- `input_client` — pipe client to `OsdropInput`.
- `drag` — edge drop targets, virtual-file data objects, drag source thread.
- `clipboard` — format listener, delayed rendering.
- `files` — readers and writers with long paths, Mark of the Web.
- `discovery` — DNS-SD register/browse.
- `keystore` — DPAPI identity, roster and permissions under `%APPDATA%\Osdrop`.
- `screens` — monitor enumeration and change notifications.
- `capi` — flat C API consumed by C#.

**Windows input process (C++, SYSTEM in the user session)**
- Hook thread, capture and parking, transparent system cursors with crash-safe restore, `SendInput` injection, pipe server with the validated command set. Nothing else.

**Windows service (C++, LocalSystem)**
- Session tracking (`SERVICE_CONTROL_SESSIONCHANGE`), launching and relaunching `OsdropInput` with backoff, install/uninstall/stop flags used by the installer.

**Windows shell (C#)**
- `Interop` (`LibraryImport`, `UnmanagedCallersOnly` callbacks, one serial queue for every engine call — Osplay decision), `ViewModels`, `Views`, `Services` (settings, update check), `Theme`, localization assembly.

### State and persistence

- Identity, roster and local permissions: Keychain / DPAPI file.
- Layout document, settings: `UserDefaults` / JSON in `%APPDATA%\Osdrop`.
- Logs: unified logging on macOS; rotating files in `%LOCALAPPDATA%\Osdrop\logs` on Windows.
- Session, control and transfer state: memory only.

---

## Protocol Requirements

Phase 5 writes the authoritative `docs/PROTOCOL.md` and fixtures in `protocol/fixtures/`. These decisions are fixed:

### General
- Little-endian integers. Every message, datagram and transfer frame starts with a 2-byte magic and a 1-byte protocol version. Version negotiation in the first control message; incompatible major versions fail with an error code the UI explains ("update the other computer").
- Hand-written fixed binary layouts documented field by field. No protobuf or JSON on the wire.
- Fixtures come from an independent Python encoder that shares no code with the C++ codec (Osplay decision); CI fails if regenerated fixtures differ.

### Discovery
- Bonjour `_osdrop._tcp`. TXT record: protocol version, device ID, device name, platform, group ID (empty when not in a group).

### Control channel (TCP)
- 4-byte length prefix, then the encrypted payload after the handshake.
- Message families (field by field in `docs/PROTOCOL.md`): Hello; HandshakeMessage (×3, Noise XX); PairingConfirm; Capabilities; SessionSetup (UDP port and tokens); RosterState (members and revocations as a two-phase set merged by union, which covers introducing and revoking members); ScreenReport; LayoutState; Enter (entry point, modifier state, optional carried drag), Leave, Refuse (reason: locked, asleep, not permitted, busy); KeyEvent (HID usage, down/up/repeat); ButtonEvent (button, down/up, click count, cursor position); ScrollEvent (deltas, unit, phase, momentum, cursor position); ReleaseAll; CursorReport; LocalTakeover; DragProbe / DragOffer (item kind, size, content type from which the target draws the system icon, name) / DragResult (the drop itself is the injected button release); ClipboardAnnounce (formats and sizes; contents are fetched over a transfer stream); Ping/Pong; Heartbeat; Error; Bye.
- Buttons, keys and scroll travel on the reliable channel and carry the cursor position they belong to, so ordering against unreliable motion never matters: the endpoint moves the cursor to that position before injecting.

### Input datagrams (UDP)
- Associated after the handshake with a token issued over the control channel (Osplay pattern).
- Pointer motion: sequence number, target-space absolute position, capture timestamp. Latest wins; older sequence numbers are dropped. Sent at most at the controller's input rate, coalesced on the I/O thread.
- AEAD-encrypted with a key derived for input datagrams; nonce from the sequence number; sliding replay window.

### Transfer streams (TCP)
- A separate TCP connection per active transfer set, so bulk data never delays keys or control. Opened with a token issued over the control channel and encrypted with per-stream keys derived from the session.
- Pull model: the receiver requests manifest entries and byte ranges; the sender serves them from disk. Frames are AEAD chunks with counters; each file ends with a BLAKE2b hash checked against the received bytes.
- Names on the wire are UTF-8 in NFC; the receiver maps them to its platform with the `core` mapping function.

### Security model
- Noise `XX_25519_ChaChaPoly_BLAKE2b` with an Osdrop-specific prologue, so an Osplay peer can never complete a handshake with Osdrop and no key is shared between the projects.
- First pairing: both screens show a 6-digit code derived from the handshake hash; the user confirms on both. The new device joins the group of whichever side is already in one; the member then introduces it to the others over their authenticated sessions, and they trust it without another code. A device already in a different group must leave it first.
- Later connections: the peer must be a current roster member, or the connection is refused with a specific error.
- Local permissions are enforced by the receiving `core`: control, files and clipboard can each be refused per device.
- Received files never open automatically and are marked as coming from another computer (Phase 13 decision).
- Keystrokes, clipboard contents and file contents are never logged; input is logged as counts only.
- No unencrypted mode exists in release builds.

---

## Feature Scope

### MVP
- Cursor and keyboard cross between any computers in the arrangement, in every direction (Mac ↔ PC, Mac ↔ Mac, PC ↔ PC), with up to 8 computers.
- Crossing by pushing through an edge: hold the cursor at the edge and keep pushing for about one second (user's description; adjustable, including immediate crossing).
- Shared arrangement editor: every computer's monitors drawn to scale and placed by dragging; the arrangement is the same on every member.
- Hotkeys: return to this computer, lock the input to the current computer.
- Files and folders dragged across an edge are copied where they are dropped (Finder, Explorer, desktop and apps), with progress and cancel on both sides.
- Shared clipboard: plain text, rich text, HTML and images; files copied on one computer are pasted on another (user decision 2026-11-02).
- Pairing with code confirmation, one group with automatic trust for new members, revoke, per-device permissions.
- End-to-end encryption on every channel.
- Elevated windows on Windows are controllable (input process as SYSTEM).
- Safety: local input wins, input returns home on peer loss, no stuck keys, visible indicator while being controlled.
- English and Turkish UI, light and dark themes, VoiceOver and screen-reader labels.
- Notarized macOS DMG; Windows installer with the service, input process and firewall rule; clean uninstall; daily update check against GitHub Releases.
- Complete open-source repository hygiene: README (English and Turkish), docs, license, contributing guide, security policy, CI.

### Post-MVP
- Windows lock screen, sign-in screen and UAC prompts (input process on the secure desktop). Deferred by user decision 2026-11-02.
- Dragging text selections and images (not only files). Deferred by user decision 2026-11-02.
- macOS login window control (a LaunchAgent in the login window context).
- Relative pointer mode for games that lock the cursor.
- Trackpad gestures beyond scrolling (pinch, swipe between spaces).
- Linux endpoints (X11, and Wayland through libei / the input-capture portal).
- Homebrew cask and winget manifest; Windows code signing (SignPath Foundation), which would also allow `uiAccess`.

### Out of scope
- Tablets and phones: iPadOS and Android give apps no way to inject system-wide input.
- Screen sharing (Osplay) and control over the internet.

---

## Testing Strategy

Testing is part of every phase's definition of done; there is no separate testing phase.

- **Unit tests (GoogleTest):** every `core` module. The crossing engine and name mapping get property tests over generated inputs (random arrangements, random names). Roster and layout merges are tested by simulating groups with random message orders and offline members.
- **Protocol:** valid and malformed fixtures from the independent encoder, run by C++ and Swift suites; fuzz targets for every decoder (control, datagrams, transfer frames, manifests) run in CI with a time budget.
- **Integration:** the sans-I/O session and mesh engines run against a simulated network and clock (loss, reordering, outages, partitions). Transfer engine end-to-end on loopback sockets with corruption injection.
- **Platform logic:** Swift unit tests (XCTest) for services with fakes; Windows-header-free logic in `windows/shared` tested on macOS and Windows through the `portable` preset; C# tests for view models and localization (Osplay pattern).
- **Hardware tests:** real capture, injection and drag tests live in separate schemes and tools (they move the real cursor), run manually on the reference machines, not in CI.
- **Manual matrix:** `docs/TESTING.md` lists every direction, input, drag, clipboard and robustness scenario with expected results, run before each release on the reference hardware: MacBook Air M3, a second Mac, a Windows PC with two monitors and a second Windows computer, wired and on Wi-Fi.
- **Sanitizers:** ASan and TSan runs of `core` on macOS CI.
- **Rule:** a feature without tests is not done; every bug fix gets a regression test.

---

## Phases

Each phase is one branch and one pull request that the user merges after CI is green. A phase is done only when every acceptance criterion is checked. Phase files map DEVPLAN Phase N to `PHASE-0(N+1)-MVP-(N+1)` (Phase 0 → `PHASE-001-MVP-01`).

Phases 1–4 retire the platform risks first. They build production modules, tested on a single computer, and end with a go/no-go gate.

### Phase 0: Repository scaffolding
**Goal**: A clean, empty-but-building monorepo with all hygiene, tooling and CI in place.
**Delivers**:
- Directory layout as in Platform & Stack.
- `README.md` skeleton, `LICENSE` (MIT), `CONTRIBUTING.md`, `SECURITY.md` (GitHub private vulnerability reporting), `CODE_OF_CONDUCT.md`, issue and PR templates, `CODEOWNERS`, Dependabot (grouped minor/patch updates, actions pinned to commit SHAs).
- `.gitignore` (Claude Code files ignored except phase files, as in Osplay), `.gitattributes`, `.editorconfig`.
- Formatting and linting adapted from Osplay: `.clang-format`, `.clang-tidy`, `.swift-format`, `Directory.Build.props` (nullable, warnings as errors, analyzers), `scripts/lint.sh`, `scripts/format.sh`, clang-tidy runner, localization and UI-token checks; tool versions pinned.
- `core/` CMake project with an empty library and one passing test; root `Package.swift` exposing `core` and `CoreBridge`.
- `macos/project.yml` generating an empty menu bar app that links `core`, launches and shows its version.
- `windows/` CMake presets (MSVC + `portable`) for engine DLL, input process, service and shared library (empty targets that build); a WPF shell solution with a separate localization assembly that launches to a tray icon; `scripts/windows/Enter-DevShell.ps1`.
- GitHub Actions: macOS job (core, portable Windows libraries, lint, Swift package, app build and tests) and Windows job (core, engine, input, service, shell, tests).
- `docs/` placeholders: `ARCHITECTURE.md`, `PROTOCOL.md`, `BUILDING.md`, `TESTING.md`, `TROUBLESHOOTING.md`, `SECURITY_MODEL.md`, `THIRD_PARTY.md`, `RELEASING.md`.
- Repository rulesets as in Osplay: `main` protection and `v*` tag protection.
**Depends on**: Nothing
**Acceptance criteria**:
- [ ] A fresh clone builds on macOS (terminal) and Windows (command line) by following `BUILDING.md` exactly.
- [ ] Both CI jobs are green.
- [ ] Every formatter, linter and checker fails CI on an injected violation (shown once per checker).
- [ ] Rulesets are active on `main` and `v*` tags.
**Tests**:
- [ ] One `core` unit test, one Swift package test, one app test, one C# test run in CI.

### Phase 1: macOS input layer (go/no-go gate)
**Goal**: The Mac can take over its own mouse and keyboard and drive itself with synthetic input, reliably, in every app.
**Delivers**:
- Accessibility permission check with live status, deep link and runtime revocation handling.
- `InputCapturing`: active event tap on a dedicated thread with observe and capture modes; capture swallows mouse, keyboard, scroll and flag events and reports deltas, buttons, HID-mapped keys and scroll phases; tap re-enable on timeout and user-input disable.
- `CursorHiding` and cursor freezing while captured, including while another app is frontmost.
- `InputInjecting`: tagged `CGEventPost` for absolute moves, buttons with click state, drags, line and pixel scroll with phases, keys from HID usages, modifier flags, media keys.
- Physical-versus-injected detection; Secure Event Input detection naming the owning process; activation of an invisible key window when capture starts.
- `ScreenDescribing`: displays in points and pixels with scale and stable keys; change notifications.
- Debug-only developer window: capture for a few seconds, then replay the recording into the same Mac.
- Hardware tests in a separate scheme (as Osplay's `OsplayHardwareTests`).
**Depends on**: Phase 0
**Acceptance criteria**:
- [ ] Capture swallows all local input in Finder, Safari, Terminal and a full-screen app; releasing capture restores input immediately.
- [ ] While captured, the cursor is frozen and invisible even when another app is frontmost.
- [ ] Replay clicks, double-clicks, drags a window, scrolls and types into TextEdit, a browser password field and Terminal.
- [ ] With the app stopped (`SIGSTOP`) during capture, macOS gives the input back by itself; resuming the app recovers the tap without a restart.
- [ ] Secure Event Input held by another app is detected and its process named.
- [ ] **If capture cannot reliably swallow input or hide the cursor, stop and document findings before any further phase.**
**Tests**:
- [ ] Unit: HID ↔ macOS key code tables, event translation and click-count logic with fakes.
- [ ] Hardware: capture/replay scheme on the M3 Air.

### Phase 2: macOS drag bridge (go/no-go gate)
**Goal**: On one Mac, a file dragged to a screen edge is captured by Osdrop, and Osdrop can start a promise drag from injected input that drops the file into Finder or an app.
**Delivers**:
- `DragCapturing`: drag detection through the drag pasteboard while a button is held; edge drop-target panels along given segments, shown only during a drag; reading file URLs, folders and file promises (received into a staging area); ending the source drag as a copy or rejecting it so it slides back.
- `DragStarting`: helper window that receives an injected button-down; `NSDraggingSession` with one `NSFilePromiseProvider` per top-level item (folders included); asynchronous writes through a `PromisedContentReading` interface (local file reader now, network reader in Phase 14); collision-free names; Finder progress for destination files; drag image from item icons and count; lazily provided staged file URLs below a size cap.
- Cancellation: Esc, drops on targets that accept nothing, and source loss end cleanly with no partial files.
- Debug-only loopback: a drag captured at the right edge reappears as an Osdrop drag at the left edge of the same Mac.
**Depends on**: Phase 1
**Acceptance criteria**:
- [ ] Loopback copies one file, many files, a folder tree and a 10 GB file into a Finder window and onto the desktop; the large copy shows progress and Finder never stops responding.
- [ ] Dropping into Mail, Messages and a browser upload field works through promises or the staged URL; a target that accepts neither refuses cleanly.
- [ ] Drags from Finder and from Photos (promised files) are captured.
- [ ] The source drag ends as a copy on success and slides back on cancel; the source file is never moved or removed.
- [ ] **If a live drag cannot be captured at the edge, or a drag cannot be started from injected input, stop and document findings before any further phase.**
**Tests**:
- [ ] Unit: name collision resolution, promise writer state machine, staging cleanup with fake file systems.
- [ ] Hardware: loopback scenarios in the hardware scheme.

### Phase 3: Windows input process and service (go/no-go gate)
**Goal**: Windows can take over and inject mouse and keyboard input wherever a user can click, elevated windows included.
**Delivers**:
- `OsdropService` (LocalSystem): `--install`, `--uninstall`, `--stop`; launches `OsdropInput` with a SYSTEM token into the active console session; relaunches it with backoff on session change, logon, logoff, fast user switching and crashes.
- `OsdropInput`: hook thread with its own message loop whose callbacks only enqueue; capture with cursor parking and transparent system cursors, restored on release, exit and after a crash; injected-event detection; `SendInput` injection (absolute virtual-desktop moves, buttons, high-resolution wheel and horizontal wheel, scan-code keys with extended flags); per-monitor DPI awareness v2.
- Pipe protocol in `windows/shared` (portable, tested on macOS): per-session pipe, ACL, client image-path check, versioned validated command set.
- Monitor enumeration (pixels, DIP frames, scale, stable keys) and change notifications.
- Developer console tool: capture, then replay on the same PC.
**Depends on**: Phase 0
**Acceptance criteria**:
- [ ] Capture swallows input, and replay works over Notepad, Explorer, Task Manager and an elevated PowerShell.
- [ ] Ending `OsdropInput` forcibly during capture gives the user a visible, working cursor within one second.
- [ ] Hook callback time stays under 1 ms at p99 over a 10-minute session, and the hook is never removed.
- [ ] Another user's process and a non-Osdrop process of the same user cannot use the pipe; invalid commands are rejected and logged.
- [ ] Fast user switching moves the input process to the new active session.
- [ ] **If capture or injection over elevated windows is not possible, stop and document findings before any further phase.**
**Tests**:
- [ ] Unit (portable): pipe codec, command validation, HID ↔ scan code tables, coordinate conversion.
- [ ] Hardware: console tool scenarios on the developer PC.

### Phase 4: Windows drag bridge (go/no-go gate)
**Goal**: On one PC, a file dragged to a screen edge is captured, and Osdrop can start a virtual-file drag from injected input that drops into Explorer or an app.
**Delivers**:
- Edge drop targets (topmost, layered, non-activating, STA thread) shown while the left button is held near a crossing edge; reading `CF_HDROP` and virtual files; ending the source drag as a copy or rejecting it.
- Drag start: helper window receiving an injected button-down; `DoDragDrop` on a dedicated STA thread; data object with `CFSTR_FILEDESCRIPTORW` and `CFSTR_FILECONTENTS` (one stream per file, read through `PromisedContentReading`), `IDataObjectAsyncCapability`; delayed `CF_HDROP` to staged copies below a size cap; drag image through `IDragSourceHelper`.
- Name and path mapping used by the descriptors (long paths, reserved names, invalid characters) as a portable function, later moved into `core` in Phase 13.
- UIPI limits measured and recorded (drags from elevated apps).
- Debug-only loopback as in Phase 2.
**Depends on**: Phase 3
**Acceptance criteria**:
- [ ] Loopback copies one file, many files, a folder tree and a 10 GB file into Explorer and onto the desktop with Explorer's progress dialog, and Explorer never stops responding.
- [ ] Dropping into Outlook, Teams and a browser upload field works through virtual files or staged paths.
- [ ] Drags out of Outlook attachments and zip folders are captured.
- [ ] The elevated-source limitation is documented in `docs/TROUBLESHOOTING.md`.
- [ ] **If a live drag cannot be captured or started from injected input, stop and document findings before any further phase.**
**Tests**:
- [ ] Unit (portable): descriptor building, name mapping corpus, stream read/seek logic with fake readers.
- [ ] Hardware: loopback scenarios with the developer tool.

### Phase 5: Protocol codec and fixtures
**Goal**: The complete wire format exists as a pure, tested library and a written specification.
**Delivers**:
- `docs/PROTOCOL.md`: every control message, input datagram, transfer frame and the discovery TXT record, field by field.
- `core/protocol`: encode/decode with strict bounds checking.
- Fixtures in `protocol/fixtures/` (valid and malformed) from the independent Python generator.
- C++ and Swift (through `CoreBridge`) suites against the same fixtures; fuzz targets for every decoder.
- `capi` for version information and error codes.
**Depends on**: Phase 0
**Acceptance criteria**:
- [ ] Every message round-trips; every malformed fixture fails with its documented error and never crashes.
- [ ] Regenerated fixtures are byte-identical in CI.
- [ ] Fuzz targets run in CI with a time budget and no findings.

### Phase 6: Crypto and handshake
**Goal**: Two `core` instances authenticate each other, show the same pairing code and derive keys for control, input datagrams and transfer streams.
**Delivers**:
- Monocypher vendored with its license.
- Noise XX, cipher states, replay window and platform-supplied randomness copied from Osplay at a recorded commit, with an Osdrop prologue and labels; key derivation for input datagrams and per-stream transfer keys; pairing code.
- `capi` for identity generation, import and export (storage stays on the platforms).
**Depends on**: Phase 5
**Acceptance criteria**:
- [ ] Published Noise XX test vectors pass.
- [ ] Tampered, replayed and out-of-window messages are rejected.
- [ ] Both sides derive the same 6-digit code; any transcript change alters it.
- [ ] A handshake with Osplay's prologue fails (domain separation test).

### Phase 7: Sessions and mesh transport
**Goal**: A set of `core` instances keeps exactly one encrypted session per pair and carries control messages, input datagrams and file streams between them.
**Delivers**:
- Sans-I/O session engine (handshake, pairing confirmation, heartbeat, clock offset, stats) under a thin Asio transport, following Osplay's design.
- TCP control, UDP input datagrams with token association and latest-wins delivery, TCP transfer streams with tokens and per-stream keys.
- Mesh manager: dial direction by device ID, duplicate resolution, reconnect with backoff, peer-loss detection within one second for input safety; real-time traffic marking for input sockets.
- `capi` for the mesh, sessions and the log callback.
**Depends on**: Phase 6
**Acceptance criteria**:
- [ ] A simulated five-member group converges to a full mesh, also when every member dials at once.
- [ ] Motion datagrams under loss and reordering never move the cursor backwards.
- [ ] A saturated transfer stream does not delay control messages beyond a few milliseconds in the loopback test.
- [ ] Peer loss is detected within one second in simulation.
- [ ] No unencrypted bytes after the handshake (wire inspection test); ASan and TSan clean.

### Phase 8: Group membership and trust
**Goal**: Computers form one group by pairing with any member; every member trusts every other member, and revocations reach everyone.
**Delivers**:
- Roster as a two-phase set keyed by device ID and pairing epoch; roster sync on connect; introductions over authenticated sessions; revocation propagation; leaving a group; the group-size limit.
- Rules for pairing a device that is already in another group.
- Local per-device permissions (control, files, clipboard) enforced in `core` with specific refusal codes.
- Persistence interface for the platforms' secure stores; `capi`.
**Depends on**: Phase 7
**Acceptance criteria**:
- [ ] Simulated groups converge for every message order, including members that were offline during changes.
- [ ] A revoked device is refused by every member once each has met any up-to-date member; a stale member cannot bring it back.
- [ ] A permission turned off refuses the matching requests with a specific code.

### Phase 9: Layout and crossing engine
**Goal**: One shared arrangement places every member's monitors in a global space, and `core` decides every crossing, entry point and edge push from it.
**Delivers**:
- Screen reports and the layout document with last-writer-wins merges and validation (no overlaps; every computer touches the arrangement or is marked detached).
- Crossing geometry: exit detection on edges not covered by the computer's own monitors, neighbour search with snap tolerance, entry mapping, walls where no neighbour exists.
- Edge push state machine (push time and distance, immediate mode), virtual cursor, pointer speed normalisation and per-computer multiplier.
- Conversion to injection coordinates (macOS points, Windows physical pixels per monitor scale).
- `capi`.
**Depends on**: Phase 8
**Acceptance criteria**:
- [ ] Property tests over random arrangements: leaving and re-entering through the same edge returns within one unit; no crossing into gaps; mixed scales, vertical stacks and L-shaped desktops behave like the platforms' own display arrangements.
- [ ] Layout merges converge in every order.
- [ ] Edge push timing is deterministic with a fake clock.

### Phase 10: Discovery, pairing and group on both platforms
**Goal**: Macs and PCs find each other, pair into one group and keep the mesh connected.
**Delivers**:
- Bonjour advertise and browse on macOS (DNS-SD C API) and Windows (DNS-SD APIs).
- Keychain and DPAPI stores for identity, roster and permissions.
- Temporary pairing UI on both platforms (code and confirm); final UI comes in Phases 16 and 17.
- Local Network permission status on macOS; a firewall rule for development builds documented in `BUILDING.md`.
- Debug view of the mesh (members, sessions, RTT).
**Depends on**: Phase 9
**Acceptance criteria**:
- [ ] Mac ↔ PC, Mac ↔ Mac and PC ↔ PC pair with a code.
- [ ] A third computer pairs with any member and is trusted by all others without another code.
- [ ] A revocation from any member removes the device everywhere.
- [ ] Restarting any app or computer reconnects the mesh without user action.

### Phase 11: Cursor and keyboard across computers
**Goal**: One computer's mouse and keyboard move across every computer in the arrangement, in every direction.
**Delivers**:
- Controller orchestration on both platforms: capture on crossing, virtual cursor, Enter and Leave, motion datagrams, keys, buttons and scroll with positions, held-key ledger and release-all on every exit path.
- Endpoint orchestration: injection, refusals (locked, asleep, not permitted), cursor reports.
- Safety: input returns home within one second on peer loss; local input on a controlled computer takes it back; hotkeys for "return to this computer" and "lock input here"; a visible indicator on a computer while it is being controlled.
- A default side-by-side arrangement until the arrangement editor exists (Phases 16 and 17).
- Input latency measurement through the session clock offset, shown in the debug view.
**Depends on**: Phases 1, 3, 10
**Acceptance criteria**:
- [ ] With at least three computers (Mac, PC and a second Mac or PC), the cursor and keyboard cross in every direction by pushing through an edge, and stop at walls.
- [ ] Input latency on wired gigabit: median under 8 ms, p99 under 20 ms; on 5 GHz Wi-Fi: median under 15 ms. Method documented in `docs/TESTING.md`.
- [ ] Unplugging the network during control returns the input to the controller within one second.
- [ ] A scripted stress run of 1000 crossings with keys and buttons held leaves no stuck key or button on any computer.
- [ ] Moving the local mouse of a controlled computer takes control back at once.

### Phase 12: Input fidelity
**Goal**: Typing, shortcuts and scrolling feel native in every direction.
**Delivers**:
- Complete HID ↔ platform key tables (ISO and JIS extra keys, numpad, F13–F24, media keys) with layouts applied by the target (Turkish Q and F, US, German verified).
- Modifier mapping across platforms with a default chosen by the user at the start of the phase (for example ⌘ acting as Ctrl on a PC), changeable per direction.
- Caps Lock state sync, key repeat forwarding, dead keys through the target's layout.
- Scroll normalisation: the controller's scroll direction preserved, high-resolution and continuous scroll, momentum.
- Crossing settings (push time, immediate crossing, crossing only with a modifier) and per-computer pointer speed.
**Depends on**: Phase 11
**Acceptance criteria**:
- [ ] A documented shortcut matrix passes in all four directions.
- [ ] ğ ü ş ı ö ç Ğ Ü Ş İ Ö Ç type correctly when both computers use a Turkish layout.
- [ ] Scrolling on a remote computer moves content the same way as on the controller.
- [ ] Every table entry round-trips in unit tests.

### Phase 13: File transfer engine
**Goal**: `core` moves files and folder trees between members quickly, safely and verifiably.
**Delivers**:
- Manifests (tree, sizes, times, attributes), ranged reads, per-file BLAKE2b verification, cancellation from either side, a concurrency limit and backpressure.
- Portable name mapping in `core` (NFC, Windows-invalid characters, reserved names, trailing dots and spaces, case collisions within one tree, path length), replacing the Phase 4 function.
- Platform readers and writers (long paths on Windows), a free-space check before writing, removal of partial files on failure.
- Marking of received files (quarantine on macOS, Mark of the Web on Windows), decided with the user at the start of the phase.
- Symbolic links inside folders arrive as link entries whose content is the target; whether a receiver recreates, follows or skips them is decided with the user at the start of the phase.
- Receiver-side enforcement of the "files" permission.
**Depends on**: Phase 7, Phase 8
**Acceptance criteria**:
- [ ] A 10 GB file between a Mac and a PC on wired gigabit reaches at least 90 MB/s.
- [ ] Injected corruption is detected and the partial file removed; cancelling mid-file leaves nothing behind.
- [ ] The name corpus (Unicode, emoji, reserved names, long paths) maps losslessly where the target allows and predictably where it does not.
- [ ] Manifest and frame decoders are fuzzed.

### Phase 14: Drag and drop across computers
**Goal**: A file dragged to an edge crosses with the cursor and lands where it is released, in every direction.
**Delivers**:
- Drag probe, offer, drop, cancel and result flows wired to the bridges from Phases 2 and 4 and to the transfer engine.
- Any member can be the source, including a computer that is itself being controlled remotely; drags across several computers (A → B → C).
- Progress and cancel on both sides; transfers continue after the cursor moves on; a drop on a target that accepts nothing cancels cleanly.
**Depends on**: Phases 2, 4, 11, 13
**Acceptance criteria**:
- [ ] Mac → PC, PC → Mac, Mac → Mac, PC → PC, a three-computer chain and a drag started on a remotely controlled computer all copy the file where it is dropped.
- [ ] A 10 GB file and a folder tree with Unicode names arrive intact.
- [ ] Losing the network mid-transfer ends with a clear message on both sides and no partial file.

### Phase 15: Shared clipboard
**Goal**: Copy on one computer and paste on another, for text, images and files.
**Delivers**:
- Clipboard watchers (pasteboard change count on macOS, format listener on Windows); announcements carry formats only, never content.
- Lazy proxies on the other members (pasteboard data providers, delayed rendering); latest copy wins.
- Formats: plain text, RTF, HTML, PNG (TIFF and DIB converted); files through promises and virtual files, with staged URLs where a paste target needs them. Whether Finder pastes file promises from the general pasteboard is verified at the start of the phase and the approach chosen accordingly.
- Concealed and transient markers respected in both directions (`org.nspasteboard.ConcealedType` and related types; Windows exclusion formats set on proxied sensitive data).
- Size caps, the per-device clipboard permission, behaviour with clipboard history verified on Windows and documented.
**Depends on**: Phase 13
**Acceptance criteria**:
- [ ] Text, rich text, HTML and images paste correctly in all four directions.
- [ ] Passwords copied from a password manager never leave the computer.
- [ ] Pasting copied files in Finder and Explorer copies them with progress.
- [ ] If the source computer disappears, a paste returns quickly with nothing pasted and a notice, never a hang.

### Phase 16: macOS app UI
**Goal**: A simple, clean, finished Mac interface.
**Delivers**:
- Menu bar panel: group members and their state, which computer has the input, transfers in progress, quick toggles.
- Settings: Arrangement (every member's monitors to scale, drag to place, snapping, live sync), Devices (pair, rename, permissions per device, revoke, leave group), Input (crossing, modifier mapping, pointer speed per computer, hotkeys), Clipboard and Files (toggles, size caps, received-file marking), General (launch at login, update check), Advanced (diagnostics, logs).
- Onboarding: Accessibility, Local Network and, if Phase 15 needs it, Files and Folders, with live status and deep links.
- Pairing sheet, being-controlled indicator, transfer notifications.
- Complete English and Turkish strings, theme tokens for every visual value, VoiceOver labels; text wraps and never truncates (Osplay lesson).
**Depends on**: Phases 12, 14, 15
**Acceptance criteria**:
- [ ] No hardcoded strings, colors, fonts or spacing in views (lint).
- [ ] Switching system language and appearance updates the UI correctly.
- [ ] Every error code maps to a localized, actionable message.
- [ ] An arrangement change on the Mac reaches every member within one second.

### Phase 17: Windows app UI
**Goal**: A simple, clean, finished Windows interface that works the same way as the Mac app.
**Delivers**:
- Tray icon whose click opens a panel mirroring the Mac menu (Osplay decision), and a Settings window with the same sections, including the arrangement editor.
- Pairing dialog, being-controlled indicator, transfer notifications, service and input-process health with repair guidance.
- P/Invoke layer with one serial queue for engine calls and callback marshaling to the dispatcher.
- Complete English and Turkish resources, theme dictionaries following the system theme.
**Depends on**: Phases 12, 14, 15
**Acceptance criteria**:
- [ ] No hardcoded strings or visual values in XAML or code-behind (lint).
- [ ] System light and dark switching is reflected live.
- [ ] A crashed input process or stopped service is surfaced clearly, and the shell never hangs on a native call.

### Phase 18: Robustness and diagnostics
**Goal**: Osdrop behaves predictably through everyday disruptions.
**Delivers**:
- Sleep and wake, network changes (Wi-Fi ↔ Ethernet, IP changes), monitors plugged, unplugged or rescaled during control, lock and unlock, fast user switching.
- Coexistence with Osplay: Osplay's virtual displays are ordinary monitors to Osdrop; the arrangement shows them and the behaviour is documented.
- Structured logging (unified logging; rotating files on Windows; `core` routed into both) and a "Copy diagnostics" action without keystrokes, clipboard or file contents.
- Manual test matrix in `docs/TESTING.md`.
**Depends on**: Phases 16, 17
**Acceptance criteria**:
- [ ] Every scenario in the matrix passes on the reference hardware, wired and on Wi-Fi.
- [ ] Waking any computer restores the mesh automatically.
- [ ] A two-hour session with continuous crossings, drags and pastes shows no memory growth and no latency drift.

### Phase 19: Packaging, release and documentation
**Goal**: Anyone can install Osdrop from GitHub Releases, and contributors can build it from the docs alone.
**Delivers**:
- macOS: Developer ID signing, Hardened Runtime, notarization with the `mac-notary` keychain profile and stapling, by one release script that uploads the DMG to the tag's release.
- Windows: Inno Setup installer built by the release workflow (unsigned until signing exists) that installs the shell, engine, input process and service and adds the firewall rule; upgrades stop and restart the service; uninstall removes the service, the input process, the firewall rule and the Run entry and restores system cursors.
- Release workflow triggered by a version tag; fixed asset names with SHA-256 files; daily update check in both apps.
- Final `README.md` and `README.tr.md` in a plain tone (no emoji, no marketing headings): what it is and is not, how it works, screenshots, requirements, installation, first run, permissions, security summary, troubleshooting, building, contributing, license, credits. Repository description and topics written for search.
- Final `docs/` set.
**Depends on**: Phase 18
**Acceptance criteria**:
- [ ] The DMG passes Gatekeeper on a clean Mac without warnings.
- [ ] The Windows installer works on a clean Windows 11 machine with no manual steps beyond SmartScreen.
- [ ] Uninstall leaves no service, process, firewall rule or altered cursor behind.
- [ ] A new contributor can build both apps using only `BUILDING.md`.

---

## Implementation Guidelines

These apply to ALL phases. Claude Code must follow them throughout development.

- **Input safety invariants come first.** Every injected key or button down has a matching up on every exit path. Capture always has a way back: the in-app watchdog and the capture guard process on macOS, the one-second peer-loss rule, local-input-wins, and crash-safe cursor restore on Windows. A change that weakens any of these is a bug, even if a feature works.
- Follow SOLID, especially Single Responsibility and Dependency Inversion. Every platform capability (capture, injection, cursor, screens, drag capture, drag start, pasteboard, files, discovery, key storage, permissions, hotkeys, lock state) sits behind an interface and is injected.
- Patterns where they remove complexity: State for sessions, crossings and drags; Strategy for platform adapters; Observer/async sequences for events; Adapter for private APIs. No patterns for their own sake.
- Clean, self-documenting code. Comments explain why, never what.
- No hardcoded values: protocol constants live in `core`; tunables (timeouts, push defaults, caps, limits) in one constants file per platform.
- **Theming is centralized.** Every visual value comes from the platform theme source.
- **Localization is active.** Every user-facing string goes through `Localizable.xcstrings` or `.resx`, in English and Turkish. Hints that apply to either computer say "Mac or PC" (Osplay lesson).
- Error handling is explicit. C++: `Result`-style returns, no exceptions across the C API, no silent catches. Swift: typed errors. C#: specific exceptions mapped from engine codes. Every error that reaches the user has an actionable localized message.
- Memory and resources: RAII in C++, no raw `new`/`delete`, `ComPtr` for COM. No force unwraps in Swift outside tests. The C API uses opaque handles with explicit create/destroy and documents thread affinity for every callback.
- Concurrency: `core` owns its I/O thread; callbacks never block it. Event tap and hook callbacks only enqueue. Swift controllers are actors; UI on the main actor. OLE work runs on dedicated STA threads. The C# shell never calls native code on the UI thread.
- Performance: no allocation per input event or per packet in steady state in `core`; file data streams in chunks and is never held whole in memory.
- Security: validate every length and index from the network; fuzz every decoder; never log key material, keystrokes, clipboard contents or file contents.
- Testing: unit tests for every `core` module, shared fixtures, simulated-network integration tests, hardware schemes and the manual matrix. CI stays green after every phase.
- Commits: small, focused, conventional commit messages. Each phase updates the docs it touches.
- Repository hygiene: README, `.gitignore`, LICENSE, CONTRIBUTING, SECURITY and third-party notices kept current. Docs and READMEs use a plain tone: no emoji, no marketing headings.
- Developer instructions say "terminal" for macOS steps and "command line" for Windows (PowerShell) steps; every Windows command list starts by entering the developer shell (Osplay practice).
- Respect existing code patterns: read and understand current code before changing it.

---

## Risks & Watch Items

- **Background cursor hiding on macOS** may need a private connection property. Isolated behind `CursorHiding`, tested on every macOS beta. Validated in Phase 1.
- **Drag capture through edge panels** relies on window-server behaviour that macOS and Windows updates can change. Panels appear only during drags, so normal clicks are never intercepted. Validated in Phases 2 and 4; retested on every OS beta.
- **Secure Event Input** on a controlling Mac hides keystrokes from the tap. Taking focus to make the password field resign is refused by macOS 14+ when another app is frontmost (Phase 1), so the remaining mitigation is telling the user; Phase 11 decides how the controller reacts (for example, keeping the input local while secure input is on).
- **SYSTEM input process on Windows** widens the privilege surface: malware running as the user could try to drive elevated windows through the pipe. Mitigated by the pipe ACL, image-path checks and accepting injection only during active control; revisit with code signing (`uiAccess`).
- **Antivirus and EDR products** may flag unsigned binaries that combine keyboard hooks with input injection. Apply to SignPath Foundation early; submit builds to Microsoft for analysis; document false positives.
- **Low-level hook removal** if a callback ever exceeds the timeout. Callbacks only enqueue; measured in Phase 3 and in the soak test.
- **Wi-Fi loss bursts (AWDL)** delay reliable messages. Motion is latest-wins over UDP; keys and buttons carry positions; measured in Phase 11 and Phase 18.
- **Lazy clipboard rendering** can be triggered eagerly by clipboard history or Universal Clipboard. Verified in Phase 15; sensitive data is excluded from history.
- **Promised files arrive late or never.** Photos downloads iCloud-only originals before it delivers them (Phase 2), so names and sizes of promised items are known only after delivery. Phase 14 must start the receiving side's drag before those are known or wait visibly; the edge capture already shows the wait and gives up after 2 minutes.
- **AppKit callbacks off the main thread.** Swift 6 adds main actor checks to AppKit callbacks it believes run on the main thread; two of them (file promise delegate, promise receiver) run on background queues and crashed Osdrop in Phase 2. Every new AppKit or Foundation callback is checked for the queue it runs on.
- **Finder pasting file promises** from the general pasteboard is unverified. Checked at the start of Phase 15; the staged-URL path is the fallback.
- **Stuck modifier keys** after crashes or network loss. Prevented by the held-key ledger and release-all on every exit path; covered by the stress run in Phase 11.
- **Keyboard layout mismatch** between computers produces the target layout's characters, as with a plugged-in keyboard. Explained in the UI and README.
- **TCC changes** (Accessibility, Local Network, Files and Folders) across macOS releases. Onboarding and runtime revocation handling must stay resilient; the signing identity never changes.
- **Copied Osplay code** can diverge from security fixes made in Osplay. Every security fix in either project is checked against the other; provenance is recorded.
- **Two people using two computers** of one group at once: local input always wins; the behaviour is documented.
