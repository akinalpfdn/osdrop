# Phase 004 (MVP-04) — Windows input process and service
Status: ACTIVE

DEVPLAN reference: Phase 3 (go/no-go gate). Depends on: Phase 0

## Goal
Windows can take over and inject mouse and keyboard input wherever a user can click, elevated windows included.

## Tasks
- [x] `OsdropService` (LocalSystem): `--install`, `--uninstall`, `--stop`; launches `OsdropInput` with a SYSTEM token into the active console session; relaunches it with backoff on session change, logon, logoff, fast user switching and crashes.
- [x] `OsdropInput`: hook thread with its own message loop whose callbacks only enqueue; capture with cursor parking and transparent system cursors, restored on release, exit and after a crash; injected-event detection; `SendInput` injection (absolute virtual-desktop moves, buttons, high-resolution wheel and horizontal wheel, scan-code keys with extended flags); per-monitor DPI awareness v2.
- [x] Pipe protocol in `windows/shared` (portable, tested on macOS): per-session pipe, ACL, client image-path check, versioned validated command set.
- [x] Monitor enumeration (pixels, DIP frames, scale, stable keys) and change notifications.
- [x] Developer console tool: capture, then replay on the same PC.
- [x] Announce `OSDROP_FEATURE_TAKES_CONTROL` in `windows/shared` (`kAvailableFeatures`) once injection works, so Macs start crossing into the PC.

## Acceptance Criteria
- [ ] Capture swallows input, and replay works over Notepad, Explorer, Task Manager and an elevated PowerShell.
- [ ] Ending `OsdropInput` forcibly during capture gives the user a visible, working cursor within one second.
- [ ] Hook callback time stays under 1 ms at p99 over a 10-minute session, and the hook is never removed.
- [ ] Another user's process and a non-Osdrop process of the same user cannot use the pipe; invalid commands are rejected and logged.
- [ ] Fast user switching moves the input process to the new active session.
- [ ] **If capture or injection over elevated windows is not possible, stop and document findings before any further phase.**
- [x] Tests — Unit (portable): pipe codec, command validation, HID ↔ scan code tables, coordinate conversion.
- [ ] Tests — Hardware: console tool scenarios on the developer PC.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Monitor enumeration was already in the engine (`screens/monitors`, Phase 10) with stable keys
  and the shell's display-change notifications; nothing new was needed.
- Hidden cursors come back three ways: saved copies on release, the next OsdropInput after a crash
  (marker file), and the client as the user when the pipe breaks.
- The pipe admits only `Osdrop.exe` and `OsdropInputTool.exe` from OsdropInput's folder, one at a
  time; the shell's build copies the native executables next to `Osdrop.exe`.
- The engine's `InputBridge` makes the PC controllable (Mac → PC) and announces TAKES_CONTROL.
- User decision (2026-11-08): run W1–W8 first; once they pass, the PC controlling other computers
  (PC → Mac, Phase 11's Windows half) is added to this PR, so Phase 3 delivers both directions.
- Hardware rows: TESTING.md W1–W8; W3 is the go/no-go gate.
