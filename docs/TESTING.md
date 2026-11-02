# Testing

> Status: the manual matrix grows with every phase that touches real hardware and is complete in
> Phase 18.

## Automated tests

Run by CI on every pull request (commands in [`BUILDING.md`](BUILDING.md)):

| Suite | Framework | Covers |
|-------|-----------|--------|
| `core/tests/cpp` | GoogleTest | Every `core` module |
| `windows/shared/tests` | GoogleTest | Windows-header-free libraries, on macOS and Windows |
| `macos/CoreBridgeTests` | Swift Testing | The Swift wrapper over the C API |
| `macos/OsdropTests` | Swift Testing | Mac app logic: key and modifier tables, event translation and building, injection with a recording poster, the tap policy, replay plans, the capture guard's decisions and the guard process against a stand-in |
| `windows/app/Osdrop.Localization.Tests` | xUnit | Shell strings in every shipped language |

The core also runs under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer on macOS.

## Hardware tests

Tests that move the real cursor, type, or capture input cannot run in CI. On macOS they live in
the `OsdropHardware` scheme (`macos/OsdropHardwareTests`):

| Suite | What it does on screen |
|-------|------------------------|
| Input on real hardware | Moves the cursor to the main display's centre and back; captures input for about 1.5 s |
| Injection into a real window | Opens a small "Osdrop hardware test" window and clicks, types, double-clicks, scrolls and drags it |
| Displays on real hardware | Nothing; reads the display list |
| Secure Event Input on real hardware | Nothing visible; turns Secure Event Input on in the test app for a moment |

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

### Runs

| Date | Build | Scenarios | Result |
|------|-------|-----------|--------|
| 2026-11-02 | Phase 1 branch, Debug, M3 Air | C1, C3, C5 | Pass (C3 after the capture guard was added; before it, the cursor stayed hidden while the app was stopped). C5: macOS refused Osdrop keyboard focus with Finder in front |
