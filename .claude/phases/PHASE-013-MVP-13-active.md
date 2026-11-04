# Phase 013 (MVP-13) — Input fidelity
Status: ACTIVE

DEVPLAN reference: Phase 12. Depends on: Phase 11

## Goal
Typing, shortcuts and scrolling feel native in every direction.

## Tasks
- [x] Complete HID ↔ platform key tables (Mac table from Phase 1; Windows scan codes in `windows/shared`, CI-verified) (ISO and JIS extra keys, numpad, F13–F24, media keys) with layouts applied by the target (Turkish Q and F, US, German verified).
- [x] Modifier mapping across platforms with a default chosen by the user at the start of the phase (for example ⌘ acting as Ctrl on a PC), changeable per direction.
- [x] Caps Lock state sync, key repeat forwarding, dead keys through the target's layout (keys travel as positions; hardware rows K3, K5, K6).
- [x] Scroll normalisation: the controller's scroll direction preserved, high-resolution and continuous scroll, momentum.
- [x] Crossing settings (push time, immediate crossing, crossing only with a modifier) and per-computer pointer speed.

## Acceptance Criteria
- [ ] A documented shortcut matrix passes in all four directions.
- [ ] ğ ü ş ı ö ç Ğ Ü Ş İ Ö Ç type correctly when both computers use a Turkish layout.
- [ ] Scrolling on a remote computer moves content the same way as on the controller.
- [x] Every table entry round-trips in unit tests.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decision (2026-11-04): stacked on `feature/phase-11-input`; core and Mac now, Windows
  tables verified through CI only, hardware rows wait for the devices.
- User decision (2026-11-04): between a Mac and a PC, shortcuts keep their place by default —
  Mac → PC: ⌘ → Ctrl, ⌃ → Windows, ⌥ → Alt; PC → Mac: Ctrl → ⌘, Windows → ⌃, Alt → ⌥. Mac ↔ Mac
  and PC ↔ PC pass keys unchanged. Changeable per direction. Rejected: keys by position.
- Caps Lock travels as a state in Enter's former reserved byte (protocol change, fixtures
  regenerated); the Mac sets it with `IOHIDSetModifierLockState`.
- A line is a notch on every platform; a trackpad point is 1/48 of a Windows notch (to tune with
  K7 on the PC).
- Settings are read from UserDefaults `input.` keys until the Phase 16 window.
