# Phase 002 (MVP-02) — macOS input layer
Status: ACTIVE

DEVPLAN reference: Phase 1 (go/no-go gate). Depends on: Phase 0

## Goal
The Mac can take over its own mouse and keyboard and drive itself with synthetic input, reliably, in every app.

## Tasks
- [ ] Accessibility permission check with live status, deep link and runtime revocation handling.
- [ ] `InputCapturing`: active event tap on a dedicated thread with observe and capture modes; capture swallows mouse, keyboard, scroll and flag events and reports deltas, buttons, HID-mapped keys and scroll phases; tap re-enable on timeout and user-input disable.
- [ ] `CursorHiding` and cursor freezing while captured, including while another app is frontmost.
- [ ] `InputInjecting`: tagged `CGEventPost` for absolute moves, buttons with click state, drags, line and pixel scroll with phases, keys from HID usages, modifier flags, media keys.
- [ ] Physical-versus-injected detection; Secure Event Input detection naming the owning process; activation of an invisible key window when capture starts.
- [ ] `ScreenDescribing`: displays in points and pixels with scale and stable keys; change notifications.
- [ ] Debug-only developer window: capture for a few seconds, then replay the recording into the same Mac.
- [ ] Hardware tests in a separate scheme (as Osplay's `OsplayHardwareTests`).
- [ ] `OsdropCaptureGuard`: a guard process that ends Osdrop when it hangs during capture, so the cursor comes back (user decision 2026-11-02, added after the `SIGSTOP` test).
- [ ] App Nap held off while the event tap runs.

## Acceptance Criteria
- [ ] Capture swallows all local input in Finder, Safari, Terminal and a full-screen app; releasing capture restores input immediately.
- [ ] While captured, the cursor is frozen and invisible even when another app is frontmost.
- [ ] Replay clicks, double-clicks, drags a window, scrolls and types into TextEdit, a browser password field and Terminal.
- [ ] With the app stopped (`SIGSTOP`) during capture, macOS gives the input back by itself; resuming the app recovers the tap without a restart.
- [ ] Secure Event Input held by another app is detected and its process named.
- [ ] **If capture cannot reliably swallow input or hide the cursor, stop and document findings before any further phase.**
- [ ] Tests — Unit: HID ↔ macOS key code tables, event translation and click-count logic with fakes.
- [ ] Tests — Hardware: capture/replay scheme on the M3 Air.

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
