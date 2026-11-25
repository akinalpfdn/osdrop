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
| `macos/OsdropTests` | Swift Testing | Mac app logic: key and modifier tables, event translation and building, injection with a recording poster, the tap policy, replay plans, the capture guard's decisions and the guard process against a stand-in. They run inside the app, which then starts empty (`OsdropMain`), so a run leaves the Osdrop in use alone |
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
| G4 | Revocation | On any member, remove another one; then pair the two again, once from each side | It disappears everywhere; the removed computer says it was removed and lists no group. Pairing again shows a code and brings it back to every member |
| G5 | Restart | Quit and start Osdrop on one member; then restart that computer | Members reconnect without a code or any action |
| G6 | Different groups | Pair a computer of one group with one of another group | Refused before any code: the two are in different groups |
| G7 | Local Network off (Mac) | Turn off Osdrop in System Settings › Local Network | The menu says Local Network access is needed; turning it on finds computers again |
| G8 | Blocking | On a member, choose **Remove and Block** for another member; on that computer, pair again. Then pair a new computer and choose **Block** at its code; unblock both in Settings › Devices (PC: the Computers window) | Each blocked computer is refused at once, by every member and without a code; the blocked list names both; after unblocking, pairing shows a code again |

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

### Windows drag bridge (Phase 4)

On the PC with a Debug build of the shell: open the panel and turn on "Drag loopback (developer)".
A drag dropped at an outer edge of the desktop (an edge with no monitor beyond it; between two
monitors there is no strip) comes back at that monitor's left edge, held by Osdrop; move the mouse
and click where it should drop. Esc cancels it.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| DW1 | One file | Drag a file from Explorer to an outer edge of the desktop and let go; click inside an Explorer window | The file is copied there; the original stays where it was |
| DW2 | Many files and a folder | As DW1 with several files and a folder with subfolders, dropped on the desktop | Everything arrives with the same tree; Explorer shows its progress |
| DW3 | Large file | As DW1 with a 10 GB file | Explorer's progress dialog shows; Explorer keeps responding during the copy |
| DW4 | Apps | As DW1, dropping into a browser upload field, Teams and an Outlook message | Each one takes the file |
| DW5 | Virtual sources | Drag an Outlook attachment and a file out of a zip folder to the edge | They come back as files and drop as in DW1 |
| DW6 | Cancel | Start a loopback drag and press Esc; drop one on a place that takes nothing | The drag ends with nothing copied; Osdrop's log (Settings › Advanced) says cancelled |
| DW7 | Elevated source | Drag a file from an Explorer window opened by an app run as administrator | The edge takes nothing (TROUBLESHOOTING.md); nothing breaks |

### Windows input (Phase 3)

On the PC, with the service installed from the shell's output folder (BUILDING.md › Input service
for development). Quit Osdrop before the tool rows: the input process serves one client at a time.
The tool's capture starts 5 seconds after the command, so the window to try it on can be clicked
first, and takes the PC's mouse and keyboard for up to the seconds given; Esc ends it. W8 onwards
run with Osdrop on the PC and a Mac in the group, the PC to the Mac's side; Ctrl+Alt+Win+Esc
always brings the PC's cursor home.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| W1 | Service | Install the service, then open Task Manager › Details | `OsdropInput.exe` runs as SYSTEM in the signed-in user's session; `service.log` and `input.log` in `%ProgramData%\Osdrop\logs` say it started |
| W2 | Capture and playback | Open Notepad, run `OsdropInputTool capture 15`, then within the 15 seconds type a sentence, click into the text, move the mouse and scroll | During the capture nothing happens on screen and no cursor shows; then the cursor shows where it was, and the playback types the same sentence and clicks and scrolls the same way |
| W3 | Elevated windows (go/no-go) | As W2 over Task Manager (scroll its list, click a column) and over a PowerShell window run as administrator (type a command) | The capture swallows input there too and the playback reaches both windows. If not, the phase stops here and the findings are written down |
| W4 | Input process ended | In an administrator PowerShell run `Start-Sleep 5; taskkill /F /IM OsdropInput.exe`, and at once in another one `OsdropInputTool capture 30` | Within a second of the kill the cursor shows and the mouse and keyboard work; the tool says OsdropInput went away; a new OsdropInput starts |
| W5 | Hook timing | Use the PC for 10 minutes with the service running, then run `OsdropInputTool stats`; then W2 again | p99 under 1000 µs; the capture still swallows input, so the hook was never removed |
| W6 | Pipe access | Copy `OsdropInputTool.exe` to the desktop and run `stats` there; run `runas /user:<another user> <Osdrop's folder>\OsdropInputTool.exe stats` | Both are refused; `input.log` names the refused client; nothing is injected |
| W7 | User switching | Sign in as a second user with fast user switching, then switch back | OsdropInput follows the active session each time (Task Manager › Details › Session ID) |
| W8 | Mac → PC | With Osdrop on both and the service running, push the Mac's cursor into the PC; type in Notepad, click, scroll, use an elevated window; then move the PC's own mouse. Stop the service and push again | The cursor crosses and input lands on the PC, elevated windows included; moving the PC's mouse brings the Mac's cursor home. With the service stopped the PC's edge is a wall and the Mac says the PC does not accept control |
| W9 | PC → Mac | With Osdrop on both and the service running, push the PC's cursor through its edge towards the Mac; type in TextEdit, double-click a word, click two different words quickly, scroll, right-click; push back | The PC's cursor hides and the Mac's moves; input lands on the Mac; the double click selects the word, the two quick clicks on different words select nothing; scrolling goes the way it goes on the PC; pushing back shows the PC's cursor at that edge |
| W10 | From an elevated window | Click into a PowerShell run as administrator, then push the cursor to the Mac and type | The cursor crosses as from any window; the PowerShell gets none of the keys |
| W11 | Return chord | While on the Mac, press Ctrl+Alt+Win+Esc | The cursor is back on the PC where it left; no Start menu opens; the Mac gets no Esc |
| W12 | Lock chord | On the PC press Ctrl+Alt+Win+K, push against the Mac's edge; press it again and push | While locked the edge is a wall; no Start menu or app menu opens and no K is typed; unlocked, it crosses |
| W13 | Held keys and Caps Lock | Hold Shift on the PC, cross, type, release Shift, type; then turn Caps Lock on while on the Mac, come back and type on the PC | Capitals while Shift is held, then lower case, nothing stuck on either computer; Caps Lock is on when typing on the PC |
| W14 | Osdrop stops during capture | While on the Mac, end Osdrop in Task Manager (Details › Osdrop.exe › End task) | Within a second the PC's cursor shows where it left and its mouse and keyboard work; nothing stays pressed on the Mac |

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
| I10 | Mac above the PC | Place the Mac above the PC in Settings › Arrangement. With the Mac's mouse, cross down near the bottom-right corner, work on the PC for two minutes, then cross back. Repeat with a file dragged from Finder | The PC's cursor moves at once after the crossing; the Mac's cursor stays hidden and still, even if Quick Note or the Dock opens; it comes back where it crossed; the drag works as before |
| I11 | Back and forth | With the Mac's mouse, cross to the PC and back several times quickly, at different heights; do the same with the PC's mouse | Coming back, the cursor moves on at once from where it entered, without stopping at the edge or bouncing back to the other computer, the same with either mouse |

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
| D2 | A tree and a large file | Drag a folder with Unicode names and inner links, then a 10 GB file | Everything arrives intact; Finder (Explorer) shows the copy's progress on the receiving side. A drop on a PC holds no links: the transfer's row and notification say how many did not arrive |
| D3 | A chain | Drag from A through B onto C | Nothing is dropped on B; the file lands on C |
| D4 | A controlled source | With A's mouse on B, drag a file on B onto C, then one on B back onto A | Each lands where it is released |
| D5 | Back to the source | Drag from A onto B and back onto A without releasing, then release in another folder on A | The drag goes on as if it never left; the file is copied or moved as Finder (Explorer) does |
| D6 | Cancel | Drag onto B, press Escape before releasing; drag onto B and release on a place that takes no files; drag onto B and press ⌃⌥⌘⎋ | Nothing is copied; the drag slides back on A |
| D7 | Other drop targets | Release on the desktop, in Mail's message window, a browser upload field, Messages | The file arrives in each, through promises or a staged copy |
| D8 | Lost network | Unplug the source's network during the 10 GB copy | The copy ends with an error on the receiving side and no partial file; the cursor comes back if it was on the lost computer |
| D9 | Local input | While a drag waits on B, move A's own mouse when A is controlled by another computer | A's drag slides back; B's drop fetches nothing |
| D10 | Photos and Mail | Drag a photo from Photos and an attachment from Mail onto B | The cursor waits at the edge while the files come out of the app, then crosses; the files land on B |
| D11 | Progress and cancel | Drag the 10 GB file onto B; watch both menus (the PC's panel, and Explorer's copy dialog when the PC receives); cancel on B, then again and cancel on A | Both menus show the transfer moving; either cancel ends it on both sides with no partial file; a notification on each side tells it did not finish; a finished one notifies too |

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
| U9 | Menu opens by itself | Quit Osdrop, run `defaults delete com.akinalpfdn.osdrop shownMenu`, start it; quit and start it again; while it runs, open it again from Applications or Spotlight | The first start opens the menu under the icon, after the permissions window is closed when that shows; the second start does not; opening it while it runs opens the menu |
| U10 | Transfers list | Send four files one after another, open the menu, press "Show all" | The menu lists the three newest, each ended one on two lines; "Show all (4)" opens a window with all four, newest first |

### Windows interface (Phase 17)

On the PC with Osdrop built from the branch, the service installed and a Mac in the group.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| WU1 | Icon and panel | Look at the notification area, click the icon, click elsewhere; switch the taskbar between light and dark (Settings › Personalization › Colors) | Osdrop's glyph shows, readable on both taskbars; the click opens the panel above the taskbar and a click elsewhere closes it; Osdrop.exe has the same icon in Explorer |
| WU2 | Light and dark, Turkish | Switch Windows between light and dark mode with the panel and Settings open; switch the display language to Turkish and start Osdrop again | Both windows change colors at once; every text is Turkish, nothing is cut off |
| WU3 | Being controlled | Push the Mac's cursor into the PC; open the panel from the PC's tray with the Mac's mouse | The tray icon turns into the filled square while the Mac controls the PC; the panel says the Mac is controlling this computer |
| WU4 | Input service | Stop the service (`OsdropService.exe --stop` as administrator), open the panel, press Repair and allow it. Then quit Osdrop, copy its folder elsewhere, start `Osdrop.exe` from the copy, push the Mac's cursor into the PC, and press Repair | The panel says the service is stopped; after Repair the warning goes within a few seconds and the cursor crosses again. The copy's panel says the service belongs to a copy in another folder and the Mac's crossing is refused; `input.log` has one refusal line; after Repair the copy connects within a few seconds |
| WU5 | Arrangement | In Settings › Arrangement drag the Mac to the PC's other side | It snaps beside the PC; the Mac shows the same arrangement within a second; the cursor crosses at the new edge |
| WU6 | Devices and input settings | Turn off "Can control this computer" for the Mac on the Mac, then rename the PC and save; change the push time and the pointer speed on the PC | The PC's cursor no longer crosses to the Mac; the Mac shows the PC's new name after it reconnects; push time and speed apply at once |
| WU7 | Clipboard and Advanced | Turn sharing off in the panel, copy on the Mac and paste on the PC; turn it on again; press Copy in Settings › Advanced and paste it into Notepad | Nothing comes over while off; the pasted diagnostics have no computer names or addresses |
| WU8 | Chords | In Settings › Input record Ctrl+Alt+Shift+H for "Bring the cursor back", cross to the Mac and press it; try to record the same combination for the other chord; press "Reset to defaults" | Ctrl+Alt+Shift+H brings the cursor back and Ctrl+Alt+Win+Esc no longer does; the second recording is refused with a message; after the reset Ctrl+Alt+Win+Esc works again |
| WU9 | Games | Start a full-screen game on the PC, push its cursor against the Mac's side; turn off "Don't cross while a game holds the cursor" and push again; quit the game and push | With the game holding the cursor it stays on the PC; with the setting off it crosses; after the game it crosses as before |
| WU10 | Panel opens by itself | Quit Osdrop, remove `"HasShownPanel": true` from `%APPDATA%\Osdrop\settings.json` (or install for a user who never ran it) and start it, or let the installer start it; quit and start it again; while it runs, start it again from the Start menu | The first start opens the panel above the taskbar; the second start does not; a start while it runs opens it |
| WU11 | Transfers list | Send four files one after another, open the panel, press "Show all" | The panel lists the three newest, each ended one on two lines; "Show all (4)" opens a window with all four, newest first |

### Robustness (Phase 18)

Two members connected, a Mac and a PC, once on Wi-Fi and once with the PC wired. "Back" means
the members are connected again and the cursor crosses, without restarting Osdrop.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| R1 | Sleep | Put the Mac to sleep (Apple menu › Sleep) for two minutes, wake it; then the same with the PC (Start › Power › Sleep) | The other computer shows it asleep and does not wake it; back within 10 s of waking |
| R2 | Wi-Fi off and on | Turn the Mac's Wi-Fi off for 30 s, then on; then the PC's network adapter | The other side shows it gone; back within 10 s of the network returning |
| R3 | Wi-Fi to cable | While connected, plug the PC's network cable in, then pull it out (it falls back to Wi-Fi) | Back within 10 s each time |
| R4 | New address | On the PC run `ipconfig /release` and then `ipconfig /renew`; on the Mac join another Wi-Fi network and come back | Back within 10 s of the new address |
| R5 | Monitor unplugged | While the Mac controls the PC with the cursor on the PC's second monitor, unplug that monitor; then plug it back | The cursor moves to a remaining monitor; the arrangement on both computers loses the monitor and gets it back where it was |
| R6 | Scale changed | While the PC is controlled, change its display scale (Settings › Display); on the Mac change a display's resolution | The cursor keeps working; edges match the new sizes on both computers |
| R7 | Locked screen | Lock the PC (Win+L) and push the Mac's cursor against its edge; unlock and push again. Then lock the Mac (⌃⌘Q) and push from the PC | Locked: the edge is a wall and the controlling computer says the other one is locked; unlocked: it crosses |
| R8 | Locked while controlled | While the Mac controls the PC, press Win+L on the PC's own keyboard | The cursor comes back to the Mac; the PC is refused until it is unlocked |
| R9 | Another user | Switch to another user on the Mac (Fast User Switching) and on the PC (Switch user), then back | While the other user is in front, crossing in is refused; back to the first user, it crosses again |
| R10 | Osplay displays | With Osplay showing the Mac's virtual displays on the PC, open Osdrop's arrangement on both | The virtual displays are ordinary monitors in the arrangement; the cursor crosses through them as through any monitor |
| R11 | Diagnostics | Settings › Advanced › Copy diagnostics on both computers; paste into a text editor | Versions, members, sessions and recent log lines; no typed text, clipboard contents or file names |
| R12 | Two hours | Use both computers for two hours with crossings, drags and pastes; note Osdrop's memory (Activity Monitor, Task Manager) and the input latency (developer window) at the start and the end | Memory within 20 % of the start; the latency median does not grow |

### Phones (Phase 20)

Run with an Android phone (Android 12 or later) and the 0.2 apps on a Mac and a PC, on one Wi-Fi
network. Osdrop stays open on the phone unless a row says otherwise.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| P1 | Pairing | On the phone, **Pair** next to the Mac; accept the code on both. Then pair the PC from the Mac's group | The phone lists both computers as connected; the Mac's menu and the PC's panel show it with a phone icon |
| P2 | Computer → phone | Mac: the phone's row › **Send files to this phone**, pick a photo, a 2 GB file and a folder of photos. PC: the same from the panel's "…" menu | Progress on the computer and on the phone; everything arrives intact in Download › Osdrop, the photos also in the gallery; a notification on the phone |
| P3 | APK | Send an APK from a computer | The phone's notification opens the installer |
| P4 | Phone → computer | Share a photo and a 2 GB video from the gallery to Osdrop, pick the Mac; again from the app's **Send files** to the PC | They arrive in the computer's Downloads, listed with progress and a notification |
| P5 | Cancel | During P2 and P4, cancel on the receiving side, then on the sending side | Both sides stop; no partial file stays in Downloads; the sender says the sending stopped |
| P6 | App closed | Close Osdrop on the phone (swipe it away), then open it again | The computers show the phone as not connected, and connected again without a code |
| P7 | Files permission | On the Mac, turn off files for the phone; send from the phone | The send is refused at once and the phone says so |
| P8 | Old computer | A computer on 0.1 in the group | The 0.1 computer keeps working with the others and does not list the phone |

### Phone corners (Phase 21)

A phone in the group with Osdrop open on it; Settings › Devices sets its box's screen and corner.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| PB1 | Drop to the phone | Drag files from Finder or Explorer into the phone's corner, drop them in the box | The box shows the phone's name once the drag reaches the corner; the files arrive on the phone, the box shows their progress and closes a moment after they arrived |
| PB2 | Files from the phone | Share photos from the phone to the computer | The box opens in the corner by itself; each photo can be dragged out into a folder and a chat app; the photos are also in Downloads |
| PB3 | Screen and corner | Choose another screen and corner for the phone, drag again | The box shows at the new place |
| PB4 | Phone away | Close Osdrop on the phone, drag a file into the corner | Nothing shows and the drag is not taken |
| PB5 | Crossing | Drag a file to an edge with another computer beside it, away from the corner | The drag crosses to the other computer as before |
| PB6 | Two screens of different scale (PC) | Put the phone's corner on a monitor at another scale than the main one, drag into it and share a photo from the phone | The box sits in that monitor's corner, inside the taskbar's edge, and the drop reaches the phone |

### Runs

| Date | Build | Scenarios | Result |
|------|-------|-----------|--------|
| 2026-11-25 | feature/mobile-02-corner-box (ed0ad51), Release, NVIDIA PC + Android phone (debug APK) | PB1, PB2, PB3, PB4, PB6 with the PC | Pass, as reported by the user: a file dragged to the corner reached the phone, a shared photo opened the box and was dragged out to the desktop, the box followed a new corner and screen, with the phone app closed the corner took nothing |
| 2026-11-25 | feature/mobile-02-corner-box (ed0ad51), Release, NVIDIA PC | PC build: ctest 404/404, clang-tidy, dotnet format, build with 0 warnings, dotnet test 49/49 | Pass after two fixes on the PC: a local shadowing a member in report_screens (MSVC C4458, 3667b76) and display config headers built without their type (clang-tidy, ed0ad51) |
| 2026-11-25 | feature/mobile-02-corner-box (0db9936), Debug, M3 Air + Android phone (debug APK) | PB1, rest of PB2 with the Mac; the crash scenario | Pass, as reported by the user: a file dragged to the very corner opened the box and reached the phone; a photo dragged out of the box was copied; the phone app no longer crashed when the Mac sent after the phone had sent |
| 2026-11-24 | feature/mobile-02-corner-box (caeaadf), Debug, M3 Air + Android phone (debug APK) | PB1, PB2 with the Mac | PB2: the box opened by itself when the phone sent files. PB1 failed: the corner sat above the Dock, fixed in 327f825. The phone app crashed when the Mac sent after the phone had sent (two list rows with the same key), fixed in 0db9936 |
| 2026-11-24 | feature/mobile-01-android (3b30686), Release, NVIDIA PC + Android phone (debug APK) | P2, P5, P7 with the PC, WU11 | Pass, as reported by the user: the panel sent a file to the phone, cancelling stopped it, with files off the phone's send was refused, the panel listed the three newest transfers and "Show all" opened every one. P8 not run |
| 2026-11-24 | feature/mobile-01-android (ffbb2e2), Release, NVIDIA PC + Android phone (debug APK) | PC build: ctest 399/399, clang-tidy, dotnet format, build with 0 warnings, dotnet test 41/41; P1 and P4 with the PC | Pass: the phone paired with the PC and files sent from the phone landed in Downloads |
| 2026-11-24 | feature/mobile-01-android (7e7ff28), Debug, M3 Air + Android phone (debug APK) | P7, U10 | Pass, as reported by the user: with files off the phone's send was refused; the menu listed the three newest transfers and "Show all" opened every one |
| 2026-11-24 | feature/mobile-01-android (a9bf3b2), Debug, M3 Air + Android phone (debug APK) | P4 from the share sheet, P5, P6 with the Mac | Pass, as reported by the user: a shared file reached the Mac, cancelling stopped the transfer, the phone reconnected without a code after the app was closed and opened |
| 2026-11-24 | feature/mobile-01-android (a9bf3b2), Debug, M3 Air + Android phone (debug APK) | P3 with the Mac; the phone stayed connected through the run | Pass, as reported by the user: the APK arrived and the notification opened the installer |
| 2026-11-24 | feature/mobile-01-android (2f61031), Debug, M3 Air + Android phone (debug APK) | P4 with the Mac, from the app's **Send files** | Pass after 2f61031: before it the picker sent Osdrop to the background, the phone dropped its connection and the send failed |
| 2026-11-24 | feature/mobile-01-android (37a1b53), Debug, M3 Air + Android phone (debug APK) | P1 with the Mac, P2 with one MP4 from the Mac | Pass, as reported by the user: the phone paired and the video arrived. P3–P8 and the PC not run yet |
| 2026-11-23 | dev (a8bf2b5 and the branches before it), Debug, M3 Air on macOS 27.2 + PC | U9, WU10, I11 with the Mac's trackpad | Pass: the menu opened by itself at the first start (after the permissions window) and when Osdrop was opened again, not at a second start; the PC's panel the same; coming home with the trackpad no longer waits at the edge (8a1db77). The click alone had not opened the menu on macOS 27 |
| 2026-11-23 | 0.1.1 candidates on the PC and the M3 Air | WU5, I10, I11 with the PC's mouse, installer upgrade from 0.1.0 | Pass (maintainer) |
| 2026-11-23 | dev (ee4e13a), Debug, M3 Air + PC | Mac → PC in daily use | Fail: the cursor crossed and came back about a hundred times a second (797 captures in 40 minutes); keys reached the PC, the mouse never stayed. The warp that parked the Mac's cursor added its distance to the next mouse delta (b2bafc2); parking removed |
| 2026-11-22 | dev (6a78f6b) on the PC (debug engine and shell) and the M3 Air | PC → Mac crossings with the new crossing log | Pass: 18 of 18 crossings with the PC's mouse in 26 minutes, each logged by core, and OsdropInput's capture lines within 1–3 ms of each. WU5 and I10 not run yet |
| 2026-11-21 | dev (21a3a93), Debug, M3 Air connected to the PC | R12, replaced by a look at the running app (user decision) | Mac: after 2 h 45 min of the day's use, footprint 52.3 MB with a lifetime peak of 52.5 MB; over the next 10 minutes 52.1–52.3 MB, 61 open files, 9–11 threads, Mach ports 313 then 299, one TCP connection. No growth. PC: numbers to come |
| 2026-11-21 | v0.1.0 release: `Osdrop-Setup.exe` from the release workflow on the PC (a development service installed), M3 Air on a dev build | Windows installer: install, Start with Windows, connect, cross, drag, uninstall | Pass, as reported from the PC. The installer took the development service over; the uninstaller removed the service, the input process, the firewall rule and the Run entry and left the cursors at their defaults. `%APPDATA%\Osdrop` and `%LOCALAPPDATA%\Osdrop` stay by design. Not run: an upgrade, a clean Windows 11 |
| 2026-11-21 | dev (f5ae0d0) | R5, R6, R9–R11 | Not run: the user chose to assume them (edge cases). Copy diagnostics checked in code instead: names are redacted, log lines carry counts only |
| 2026-11-21 | dev (61727d5), Debug, M3 Air + PC (debug engine and shell) | R1–R4, R7, R8, Mac ↔ PC | Pass, as reported by the user. R1 from the sleep run of 2026-11-12. Win+L with the cursor on the Mac first left Control held there (every click a right-click); fixed in 5d63a03. Osplay's displays stayed in the arrangement after they were gone; fixed in 61727d5 |
| 2026-11-20 | dev (50e1bc6), Debug, M3 Air + PC (debug engine and shell) | D1, D2, D4–D8, D10, D11, Mac ↔ PC | Pass. D10 first sent Photos' preview instead of the photo, then the photo with its preview (fixed in 2b72306 and 50e1bc6); D2 showed a link left out without a notice (9cb12e6). D3, D9, Mac ↔ Mac and PC ↔ PC wait for a third computer |
| 2026-11-09 | Phase 3 branch (779a4dd), PC debug engine and Release shell, M3 Air | W1–W6, W8 | Pass. Hook p99 7 µs over 70 424 events (longest 1918 µs); the service restarted a killed OsdropInput within 3 ms. W7 not run: the PC has no second Windows user |
| 2026-11-08 | Phase 14 branch (fdb0e04), Debug, M3 Air + PC (Release shell, debug engine) | G4, G8 | Pass. G4 first failed: the member that removed a computer refused it before any code, so it could never pair again; fixed, then blocking was added (G8) |
| 2026-11-08 | Phase 14 branch, Debug, M3 Air + PC (Release shell, debug engine) | CL5, CL6, CL9 | Pass. CL6 on the Mac showed no notification until Osdrop's notifications were allowed in System Settings › Notifications: they had been refused at macOS's first prompt |
| 2026-11-07 | Phase 14 branch, Debug, M3 Air + PC (Release shell, debug engine) | G1, CL1–CL8 | G1 pass after the Mac's pairing window was fixed (it hid when Osdrop went to the background). CL1–CL4, CL7, CL8 pass. CL5: a paste before the files arrived gave the old clipboard with no progress shown; CL6: the notice was only inside the menu and the Computers window. Both now show a progress window and notifications; to run again |
| 2026-11-07 | Phase 16 branch, Debug, M3 Air | U1, U2 (Turkish, Light and Dark), U5–U8 | Pass (one computer; U3 and U4 need a second) |
| 2026-11-03 | Phase 13 branch, Release, M3 Air | Loopback throughput, 1 GiB | 210 MB/s (sender and receiver on the same computer) |
| 2026-11-02 | Phase 2 branch, Debug, M3 Air | D1–D7 | Pass after two fixes found by D4 (WhatsApp) and D5 (Photos): AppKit called promise callbacks off the main thread |
| 2026-11-02 | Phase 1 branch, Debug, M3 Air | C1, C3, C5 | Pass (C3 after the capture guard was added; before it, the cursor stayed hidden while the app was stopped). C5: macOS refused Osdrop keyboard focus with Finder in front |
