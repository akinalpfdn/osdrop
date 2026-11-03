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

### Protocol fixtures and fuzzing

`protocol/fixtures/` holds valid and malformed samples of every message, written by an independent
Python encoder from [`PROTOCOL.md`](PROTOCOL.md) (see [`protocol/README.md`](../protocol/README.md)).
The C++ and Swift suites must reach the outcome the manifest lists for each file, and valid files
must re-encode byte for byte. `scripts/lint.sh` fails when the files differ from what the generator
writes.

Each decoder (control messages, input datagrams, transfer messages, the TXT record) has a libFuzzer
target in `core/fuzz/`, and a fifth one feeds the Noise handshake reader and the input datagram
opener, which must never accept a forgery. CI runs each for 30 seconds on Linux with Clang, AddressSanitizer and
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

### Runs

| Date | Build | Scenarios | Result |
|------|-------|-----------|--------|
| 2026-11-02 | Phase 2 branch, Debug, M3 Air | D1–D7 | Pass after two fixes found by D4 (WhatsApp) and D5 (Photos): AppKit called promise callbacks off the main thread |
| 2026-11-02 | Phase 1 branch, Debug, M3 Air | C1, C3, C5 | Pass (C3 after the capture guard was added; before it, the cursor stayed hidden while the app was stopped). C5: macOS refused Osdrop keyboard focus with Finder in front |
