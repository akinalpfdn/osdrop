# Phase 002 (MVP-02) — macOS input layer
Status: DONE

DEVPLAN reference: Phase 1 (go/no-go gate). Depends on: Phase 0

## Goal
The Mac can take over its own mouse and keyboard and drive itself with synthetic input, reliably, in every app.

## Tasks
- [x] Accessibility permission check with live status, deep link and runtime revocation handling.
- [x] `InputCapturing`: active event tap on a dedicated thread with observe and capture modes; capture swallows mouse, keyboard, scroll and flag events and reports deltas, buttons, HID-mapped keys and scroll phases; tap re-enable on timeout and user-input disable.
- [x] `CursorHiding` and cursor freezing while captured, including while another app is frontmost.
- [x] `InputInjecting`: tagged `CGEventPost` for absolute moves, buttons with click state, drags, line and pixel scroll with phases, keys from HID usages, modifier flags, media keys.
- [x] Physical-versus-injected detection; Secure Event Input detection naming the owning process; activation of an invisible key window when capture starts.
- [x] `ScreenDescribing`: displays in points and pixels with scale and stable keys; change notifications.
- [x] Debug-only developer window: capture for a few seconds, then replay the recording into the same Mac.
- [x] Hardware tests in a separate scheme (as Osplay's `OsplayHardwareTests`).
- [x] `OsdropCaptureGuard`: a guard process that ends Osdrop when it hangs during capture, so the cursor comes back (user decision 2026-11-02, added after the `SIGSTOP` test).
- [x] App Nap held off while the event tap runs.

## Acceptance Criteria
- [x] Capture swallows all local input in Finder, Safari, Terminal and a full-screen app; releasing capture restores input immediately.
  - The tap swallows at the HID level, before any app. Observed by the user with Osdrop and with Finder in front (C1, C5); not tried app by app.
- [x] While captured, the cursor is frozen and invisible even when another app is frontmost.
  - C5 with Finder in front (2026-11-02).
- [x] Replay clicks, double-clicks, drags a window, scrolls and types into TextEdit, a browser password field and Terminal.
  - Verified by the hardware tests in a test window of their own (text view, password field, title bar drag, scrolling, Shift); TextEdit, a browser and Terminal were not driven, because injected keys there would reach whatever had focus.
- [x] With the app stopped (`SIGSTOP`) during capture, macOS gives the input back by itself; resuming the app recovers the tap without a restart.
  - Without help macOS did not: the cursor stayed hidden while the app was stopped; resuming released it through the watchdog. With `OsdropCaptureGuard` (user decision), Osdrop was ended 2.1 s after the stop and the cursor came back.
- [x] Secure Event Input held by another app is detected and its process named.
  - Detected (hardware test). Named only when macOS's record matches the frontmost app; the record was stale for a background holder.
- [x] **If capture cannot reliably swallow input or hide the cursor, stop and document findings before any further phase.**
  - Gate passed: capture swallows input and hides the cursor, also with another app in front.
- [x] Tests — Unit: HID ↔ macOS key code tables, event translation and click-count logic with fakes.
- [x] Tests — Hardware: capture/replay scheme on the M3 Air.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Menu bar icon: a vertical line with a half arrow crossing it each way, as a vector template image (user request).
- Our layout type is `KeyboardPhysicalLayout`; Carbon already defines `KeyboardLayoutKind`.
- Background cursor hiding uses `SetsCursorInBackground`, looked up with `dlsym` so a macOS without it degrades instead of failing to launch.
- Point scroll deltas are whole points; fractional lines travel in the fixed-point fields.
- Developer capture starts after a 3-second countdown; replay afterwards is opt-in, because replayed keys reach whichever app has focus.
- Hardware tests type only when their own window has keyboard focus and click only where their own window is topmost (after injected input reached VS Code twice).
- `SIGSTOP` during capture: the cursor stayed hidden and frozen while the app was stopped; resuming released it through the watchdog. User decision: add `OsdropCaptureGuard` (DECISIONS.md).
- App Nap delayed timers by minutes in the windowless test host; the tap now holds a latency-critical activity (DECISIONS.md).
- macOS refused Osdrop keyboard focus at capture start with Finder in front (cooperative activation), so making a password field resign is best effort; DEVPLAN constraints and risks updated.
- The secure input owner is named only when macOS's record matches the frontmost app; the record named an unrelated app (Chrome) when the test host held it.
