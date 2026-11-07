# Testing

> Status: the manual matrix grows with every phase that touches real hardware and is complete in
> Phase 18.

## Automated tests

Run by CI on every pull request (commands in [`BUILDING.md`](BUILDING.md)):

| Suite | Framework | Covers |
|-------|-----------|--------|
| `core/tests/cpp` | GoogleTest | Every `core` module; the protocol fixtures and the C API |
| `windows/shared/tests` | GoogleTest | Windows-header-free libraries, on macOS and Windows |
| `macos/CoreBridgeTests` | Swift Testing | The Swift wrapper over the C API; the protocol fixtures through it |
| `macos/OsdropTests` | Swift Testing | Mac app logic: key and modifier tables, event translation and building, injection with a recording poster, the tap policy, replay plans, the capture guard's decisions and the guard process against a stand-in |
| `windows/app/Osdrop.Localization.Tests` | xUnit | Shell strings in every shipped language |

The core also runs under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer on macOS.

### Simulated networks and loopback sockets

`core/tests/cpp/mesh_simulation.hpp` runs several meshes over a simulated clock and network:
reliable ordered TCP, lossy and reordering UDP, computers that freeze (power loss) and a capture
of every byte sent. The mesh tests check that five members converge to one session per pair even
when all dial at once, that peer loss is reported within one second, that motion never moves
backwards, and that nothing readable crosses the wire. The group tests pair computers into
groups, introduce and revoke members (also while some are offline), and check permissions; the
roster tests merge random operations in random orders and require every replica to converge.
The Windows group logic (`windows/shared`) pairs two engines over loopback with fake DNS-SD
and restarts one from its stored state; the Mac app's `GroupController` and the Windows shell's
view models are tested with fake engines (pairing flow, stored state, refusals).
The layout tests generate random arrangements of macOS and mixed-scale Windows desktops and require
that leaving and coming back through an edge returns within one unit, that nothing crosses into a
gap and that no wall stands where another computer touches; layout documents converge in every
order, edge push timing runs on a fake clock, and members share screens and arrangements over the
simulated network, including members that were offline. `transport_test.cpp` repeats the essentials
on real loopback sockets, including control latency while a transfer stream is saturated and a
recording relay on the control connection.

The transfer tests move real files between members over the simulated network: folder trees with
empty, chunk-sized and multi-megabyte files, executable flags, times, NFD names and links inside
and outside the item; names the destination already holds; content that does not match its digest
and bytes changed on the way; cancelling from either side; a full disk; the files permission and
the audience of an offer; a slow disk (the stream pauses instead of memory filling); the stream
limit per peer; hundreds of small files; a source that changes during the transfer; a lost peer.
After every failure nothing the transfer created may remain. The name mapping runs over a corpus
of Unicode, emoji, reserved names and long names, and NFC against random strings normalized by
Python's `unicodedata` (the same Unicode version as the tables).
Through the C API, members on loopback send files and quit in the middle of a transfer.

The clipboard tests announce copies over the simulated network and fetch their formats and files
within limits; two members that copy at the same moment end with the same newest copy everywhere,
a later copy wins even with a clock behind, the clipboard permission keeps copies at home, and a
fetch from a computer that left ends at once. The Mac's `ClipboardController` and the PC's
`ClipboardSync` run against a pasteboard and a mesh in memory (concealed copies are never read,
results that arrive early, newer copies, notices), and two PC engines share a copy over loopback.

The input control tests move the cursor through a row of three simulated computers and back, with
held modifiers carried over and everything released on every way out (hotkey, pushing back,
ReleaseAll, local takeover, a lost endpoint or controller, a locked screen, a withdrawn
permission); refusals (locked, busy, not permitted) send the cursor home; a held button or the
lock keeps it where it is; cursor reports move the virtual cursor; motion latency is measured
through the clock offset. A scripted run of 1000 crossings with keys and buttons held, ended in
turns by the hotkey, local takeover and pushing back, leaves nothing pressed on any computer
(DEVPLAN Phase 11). Through the C API and CoreBridge, a push against an edge on loopback moves the
input to another member and the hotkey brings it back. The Mac's `InputController` runs against
an event tap, an injector and a mesh in memory.

The input fidelity tests (Phase 12) cross from a Mac to a PC and on to a Mac in the simulated
network: Control and Command swap between platforms by default and stay as they are Mac to Mac or
when the user chose keys by position; a mapping changed during control waits for the next
crossing; Caps Lock travels as a state and comes back home; crossing only with a modifier, the
push time and the pointer speed per computer apply. Windows scan codes round-trip for every key
in the table (ISO, JIS, keypad, F13–F24, media keys) and scrolling converts to wheel units with
the right directions and without losing slow trackpad steps. The Mac's `InputController` applies
the stored settings at start and keeps Caps Lock in step.

### Protocol fixtures and fuzzing

`protocol/fixtures/` holds valid and malformed samples of every message, written by an independent
Python encoder from [`PROTOCOL.md`](PROTOCOL.md) (see [`protocol/README.md`](../protocol/README.md)).
The C++ and Swift suites must reach the outcome the manifest lists for each file, and valid files
must re-encode byte for byte. `scripts/lint.sh` fails when the files differ from what the generator
writes.

Each decoder (control messages, input datagrams, transfer messages, the TXT record) has a libFuzzer
target in `core/fuzz/`; another feeds the Noise handshake reader and the input datagram opener,
which must never accept a forgery; one feeds the stored group state and rosters from members,
which must merge the same way in either order, and one the stored layout, screen reports (the
cursor must never leave a reported desktop) and layout documents. CI runs each for 30 seconds on Linux with Clang, AddressSanitizer and
UndefinedBehaviorSanitizer; every test build also replays the fixtures and `core/fuzz/seeds/`
through the same entry points. To fuzz locally (Clang with libFuzzer, e.g. on Linux):

```sh
cmake -S core -B build/fuzz -DCMAKE_CXX_COMPILER=clang++ -DOSDROP_CORE_BUILD_FUZZER=ON \
  -DOSDROP_CORE_BUILD_TESTS=OFF
cmake --build build/fuzz
build/fuzz/fuzz/osdrop_control_fuzzer -max_total_time=60 -max_len=65537 protocol/fixtures
```

## Hardware tests

Tests that move the real cursor, type, or capture input cannot run in CI. On macOS they live in
the `OsdropHardware` scheme (`macos/OsdropHardwareTests`):

| Suite | What it does on screen |
|-------|------------------------|
| Input on real hardware | Moves the cursor to the main display's centre and back; captures input for about 1.5 s |
| Injection into a real window | Opens a small "Osdrop hardware test" window and clicks, types, double-clicks, scrolls and drags it |
| Displays on real hardware | Nothing; reads the display list |
| Secure Event Input on real hardware | Nothing visible; turns Secure Event Input on in the test app for a moment |
| Drag bridge on real hardware | Opens two small "Osdrop drag test" windows; drags promised files into one, and a file from the other onto an edge strip that accepts it once and refuses it once |

Rules for running them:

- Osdrop needs Accessibility access (System Settings › Privacy & Security › Accessibility). The
  tests run inside `Osdrop.app`, so they use that access; with a stable signing identity
  ([`BUILDING.md`](BUILDING.md)) one grant covers every build.
- Nobody touches the mouse or keyboard while they run (about 15 s).
- The window tests type only when their own window has keyboard focus and click, scroll or drag
  only where their own window is the topmost one; otherwise they stop without sending anything.

```sh
cd macos
xcodegen generate
xcodebuild -project Osdrop.xcodeproj -scheme OsdropHardware -destination 'platform=macOS' \
  -derivedDataPath DerivedData test
```

## Manual matrix

Run on the reference hardware (MacBook Air M3, a second Mac, a Windows PC with two monitors and a
second Windows computer, wired and on Wi-Fi) before a release and after changes to the parts
listed. Record each run in the table at the end.

### macOS input capture (debug builds, menu → Developer…)

Start the event tap first (**Start**). Leave "Replay what was recorded afterwards" off unless the
scenario says otherwise: replayed keys reach whichever app has focus.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| C1 | Capture | **Capture**, 10 s; after the countdown move the mouse, click, scroll and press keys | The cursor hides and stays put; nothing on screen reacts; after 10 s the cursor is back where it was and works |
| C2 | Release chord | **Capture**, then press ⌃⌥⌘⎋ | Capture ends at once; the window reports the chord |
| C3 | Hung app | **Capture**; in a terminal, stop the app when the capture starts: `kill -STOP $(pgrep -x Osdrop)` | About 2 s later `OsdropCaptureGuard` ends Osdrop and the cursor comes back; start Osdrop again |
| C4 | Secure input | Turn on Terminal › Secure Keyboard Entry and focus Terminal | The developer window says secure keyboard entry is on |
| C5 | Another app in front | **Capture**; during the countdown click Finder | The cursor hides and stays put; the window says whether macOS gave Osdrop keyboard focus |

### macOS drag bridge (debug builds, menu → Developer… → Drag bridge)

Each scenario: **Arm**, drag the items to the green strip at the right edge of the main display
and release; then **Drop with Osdrop** and put the cursor on the target within 3 seconds.

| # | Scenario | Items | Target | Expected |
|---|----------|-------|--------|----------|
| D1 | Several items | Two files and a folder with a subfolder | A Finder window | All arrive; the folder with everything inside it |
| D2 | Desktop | One file | An empty spot on the desktop | The file appears on the desktop |
| D3 | Large file | A 10 GB file (`mkfile -n 10g`) | A Finder window | Finder shows progress on the icon and stays usable |
| D4 | App | One file | A WhatsApp chat, a Mail message or a browser upload field | The file is attached |
| D5 | Promised files | A photo from Photos | A Finder window | Captured after Photos delivers it (an iCloud-only original is downloaded first) |
| D6 | Refusal | One file, with "Accept drops at the edge" off | The edge only | The drag slides back; nothing is captured |
| D7 | Nothing accepts | One file | The developer window | The drag ends as cancelled; nothing is written |

### Pairing and group (Phase 10)

Run with the apps of the same build on every computer, all on one network. Mac rows need two
Macs; PC rows need two PCs.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| G1 | Mac ↔ PC | On the Mac, menu › Nearby › **Pair** next to the PC | Both show the same code; after **Pair** on both, each lists the other as connected |
| G2 | Mac ↔ Mac, PC ↔ PC | As G1 between two Macs, then between two PCs | As G1 |
| G3 | Third computer | Pair a third computer with any member | Every member lists it as connected without another code |
| G4 | Revocation | On any member, remove another one | It disappears everywhere; the removed computer says it was removed and lists no group |
| G5 | Restart | Quit and start Osdrop on one member; then restart that computer | Members reconnect without a code or any action |
| G6 | Different groups | Pair a computer of one group with one of another group | Refused before any code: the two are in different groups |
| G7 | Local Network off (Mac) | Turn off Osdrop in System Settings › Local Network | The menu says Local Network access is needed; turning it on finds computers again |

### File transfer (Phase 13)

Measured in a release build: `OSDROP_TRANSFER_BENCH_MB=1024 core/build/release/tests/cpp/osdrop_core_tests
--gtest_filter=CapiMesh.TransferThroughputOnLoopback` moves that many megabytes between two
meshes on loopback and prints the rate. The rows below need two computers and run once the
apps can start transfers (drag and drop, Phase 14).

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| F1 | Throughput Mac → PC | A 10 GB file over wired gigabit | At least 90 MB/s |
| F2 | Throughput PC → Mac | As F1 the other way | At least 90 MB/s |
| F3 | Mac ↔ Mac | A folder with nested folders, links and long names | Arrives intact; links inside the folder work |
| F4 | Cancel | Cancel in the middle of a large file, on either side | Nothing is left in the destination |

### Shared clipboard (Phase 15)

Run with Osdrop on every computer, members of one group. The rows marked PC need the PC.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| CL1 | Text both ways | Copy text in TextEdit on a Mac, paste in Notepad on the PC; then the other way, and Mac ↔ Mac | The same text, accents and emoji included |
| CL2 | Rich text and HTML | Copy formatted text from Pages or Word and from a web page; paste into Word / TextEdit on the other computer | Formatting arrives |
| CL3 | Images | Copy a screenshot (Cmd+Ctrl+Shift+4) and an image in Preview / Paint; paste on the other computer | The image arrives, also in apps that take only TIFF (Mac) or DIB (PC) |
| CL4 | Passwords | Copy a password in 1Password or Passwords (Mac), and in a password manager on the PC | Nothing changes on the other computers |
| CL5 | Files | Copy files and a folder in Finder / Explorer; watch the corner of the other computer's screen; paste in its Finder / Explorer once it says they are ready | A window shows them coming with their progress and a cancel button; a notification says when they can be pasted; they paste. Pasting earlier gives the clipboard's previous content |
| CL6 | Over the limit | Copy more than 512 MB of files | The other computer keeps its clipboard and a notification says the copy is too large |
| CL7 | Source gone | Copy on one computer, quit Osdrop there at once, paste on another | The paste is immediate (the old clipboard) and a notice says the copy could not be fetched |
| CL8 | Clipboard history (PC) | With Windows clipboard history on (Win+V), copy on the Mac | The copy appears once in the history; nothing else is fetched |
| CL9 | The same copy again | Copy a file, wait until the other computer says it can be pasted, then copy the same file again | Nothing is fetched again: no progress window, no notification |

### Cursor and keyboard (Phase 11)

Run with Osdrop on every computer, members of one group, with Accessibility granted. A push lasts
about a second before the cursor crosses. The rows marked PC need the PC; Mac ↔ Mac needs the
second Mac. Before each run, say which computer's mouse and keyboard will be used.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| I1 | Every direction | With three computers side by side, push the cursor through each shared edge both ways; push against an outer edge | It crosses each shared edge, enters at the same height, and stops at outer edges |
| I2 | Typing and clicks | On the other computer, type in a text field, double-click a word, drag-select, scroll, right-click | All of it lands where the cursor is; the double click selects the word |
| I3 | Held modifiers | Hold Shift on the controller, cross, type letters, release Shift, type again | Capitals while Shift is held, then lower case; Shift is not stuck on either computer |
| I4 | Hotkeys | While on another computer press ⌃⌥⌘⎋; then press ⌃⌥⌘L at home and push against the edge; press ⌃⌥⌘L again | The cursor comes back where it left; while locked it does not cross |
| I5 | Local takeover | While another computer controls this one, move this computer's own mouse | Control ends at once; the controller's cursor reappears where it left; the menu bar icon returns to normal |
| I6 | Network unplugged | While controlling another computer, unplug its network (or turn its Wi-Fi off) | Within a second the cursor is back on the controller; nothing stays pressed on the other computer when it returns |
| I7 | Refusals | Lock the other computer's screen, then push into it; deny control in its permissions, then push | The cursor stays home; the menu says it is locked; with control denied the edge is a wall |
| I8 | Being controlled | While controlled, look at the controlled Mac's menu bar and menu | Another icon, and the menu names the controller |
| I9 | Latency | Developer… › Mesh on the controlled computer while moving the cursor on it for 10 s: wired gigabit, then 5 GHz Wi-Fi | Wired: median under 8 ms, p99 under 20 ms; Wi-Fi: median under 15 ms |

### Input fidelity (Phase 12)

Run in the four directions (Mac → PC, PC → Mac, Mac → Mac, PC → PC) with the default modifier
mapping, then Mac → PC with keys by position (`defaults write com.akinalpfdn.osdrop
input.modifierMapping positions`). Use TextEdit or Notes on a Mac and Notepad or WordPad on a PC.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| K1 | Shortcuts | On the other computer: copy, paste, cut, undo, select all, find, save, new tab and close tab in a browser, switch apps (⌘Tab or Alt+Tab, Win+Tab) | Each does what it does on that computer; with the default mapping ⌘C copies on a PC and Ctrl+C on a PC keyboard copies on a Mac |
| K2 | Turkish | Both computers use the Turkish Q layout, then Turkish F: type ğ ü ş ı ö ç Ğ Ü Ş İ Ö Ç | Every letter as typed |
| K3 | Other layouts | US and German on the other computer: type @ # { } [ ] \ \| and the German dead keys ^ ´ ` followed by a vowel | The other computer's layout applies, dead keys combine |
| K4 | Extra keys | An ISO keyboard's key beside left Shift, a JIS keyboard's ¥ ろ 英数 かな (if at hand), keypad with Num Lock on and off, F13–F19, volume and play/pause | The same key on the other computer; keys it lacks do nothing |
| K5 | Caps Lock | Caps Lock on at home, cross, type; press Caps Lock over there, type; come back, type | Capitals over there; lower case after the press; lower case at home too |
| K6 | Key repeat | Hold a letter and an arrow key on the other computer | One repeat stream at the controller's rate, stopping on release |
| K7 | Scrolling | Scroll with a wheel and with a trackpad (two fingers, a fling) on the other computer, with natural scrolling on one side only | Content moves the same way as at home; fine trackpad steps and momentum arrive |
| K8 | Crossing settings | Push time 300 ms (`input.pushMilliseconds`), immediate crossing (`input.immediate`), crossing only with Control (`input.crossingModifiers` 17) | The cursor crosses after the set time, at once, or only with Control held |
| K9 | Pointer speed | `input.pointerSpeeds` with the other computer at 2000 | The cursor moves twice as far there for the same hand movement |

Latency is measured on the controlled computer for every motion datagram: the arrival time minus
the controller's capture time, which core converts to the controlled computer's clock with the
session clock offset (the offset of the fastest of the last eight pings). The capture time is when
the event tap received the event, so the time inside the controller's input system is not
included. The developer view shows the median and the 99th percentile of the last 1000 datagrams.

### Drag and drop (Phase 14)

Run with three computers in a row (A | B | C), members of one group, files allowed. The rows marked
PC need the PC; Mac ↔ Mac needs the second Mac. Before each run, say whose mouse will be used:
these tests take over input on every computer they cross.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| D1 | Every direction | Drag a file from Finder (Explorer) on A through the edge into a folder on B, in each direction between a Mac and a PC, Mac → Mac and PC → PC | The cursor carries the drag; the file is copied where it is released; the source keeps its file and its drag ends as a copy |
| D2 | A tree and a large file | Drag a folder with Unicode names and inner links, then a 10 GB file | Everything arrives intact; Finder (Explorer) shows the copy's progress on the receiving side |
| D3 | A chain | Drag from A through B onto C | Nothing is dropped on B; the file lands on C |
| D4 | A controlled source | With A's mouse on B, drag a file on B onto C, then one on B back onto A | Each lands where it is released |
| D5 | Back to the source | Drag from A onto B and back onto A without releasing, then release in another folder on A | The drag goes on as if it never left; the file is copied or moved as Finder (Explorer) does |
| D6 | Cancel | Drag onto B, press Escape before releasing; drag onto B and release on a place that takes no files; drag onto B and press ⌃⌥⌘⎋ | Nothing is copied; the drag slides back on A |
| D7 | Other drop targets | Release on the desktop, in Mail's message window, a browser upload field, Messages | The file arrives in each, through promises or a staged copy |
| D8 | Lost network | Unplug the source's network during the 10 GB copy | The copy ends with an error on the receiving side and no partial file; the cursor comes back if it was on the lost computer |
| D9 | Local input | While a drag waits on B, move A's own mouse when A is controlled by another computer | A's drag slides back; B's drop fetches nothing |
| D10 | Photos and Mail | Drag a photo from Photos and an attachment from Mail onto B | The cursor waits at the edge while the files come out of the app, then crosses; the files land on B |
| D11 | Progress and cancel | Drag the 10 GB file onto B; watch both menus; cancel on B, then again and cancel on A | Both menus show the transfer moving; either cancel ends it on both sides with no partial file; a notification on each side tells it did not finish; a finished one notifies too |

### Mac interface (Phase 16)

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| U1 | First launch | Remove Osdrop's Accessibility access, start it | The permissions window opens; each row turns green once allowed in System Settings |
| U2 | Language and appearance | Switch the system language between English and Turkish, and Light and Dark appearance | Every text changes language, nothing is cut off, colors follow the appearance |
| U3 | Arrangement | With two computers, drag one to the other side of the first in Settings › Arrangement | It snaps beside the other one; the other computer shows the same arrangement within a second; the cursor crosses at the new edge |
| U4 | Devices | Turn off "Can control this computer" for a member, rename this computer | That member's cursor no longer crosses in; the others show the new name after they reconnect |
| U5 | Input settings | Change the push time, the modifier, the pointer speed and both chords | Each applies at once (the modifier mapping from the next crossing) |
| U6 | Clipboard and files | Turn sharing off; set the files limit to 128 MB; turn marking on and copy an app from the other computer | Nothing is shared while off; larger copies show the notice; the pasted app asks before it first opens |
| U7 | Advanced | Copy diagnostics, export the log | The text has no computer names or addresses; Finder shows the log file |
| U8 | VoiceOver | Go through the menu and every Settings tab with VoiceOver | Every control is announced with its name |

### Runs

| Date | Build | Scenarios | Result |
|------|-------|-----------|--------|
| 2026-11-07 | Phase 14 branch, Debug, M3 Air + PC (Release shell, debug engine) | G1, CL1–CL8 | G1 pass after the Mac's pairing window was fixed (it hid when Osdrop went to the background). CL1–CL4, CL7, CL8 pass. CL5: a paste before the files arrived gave the old clipboard with no progress shown; CL6: the notice was only inside the menu and the Computers window. Both now show a progress window and notifications; to run again |
| 2026-11-07 | Phase 16 branch, Debug, M3 Air | U1, U2 (Turkish, Light and Dark), U5–U8 | Pass (one computer; U3 and U4 need a second) |
| 2026-11-03 | Phase 13 branch, Release, M3 Air | Loopback throughput, 1 GiB | 210 MB/s (sender and receiver on the same computer) |
| 2026-11-02 | Phase 2 branch, Debug, M3 Air | D1–D7 | Pass after two fixes found by D4 (WhatsApp) and D5 (Photos): AppKit called promise callbacks off the main thread |
| 2026-11-02 | Phase 1 branch, Debug, M3 Air | C1, C3, C5 | Pass (C3 after the capture guard was added; before it, the cursor stayed hidden while the app was stopped). C5: macOS refused Osdrop keyboard focus with Finder in front |
