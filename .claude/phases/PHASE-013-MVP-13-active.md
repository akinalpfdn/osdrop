# Phase 013 (MVP-13) — Input fidelity
Status: ACTIVE

DEVPLAN reference: Phase 12. Depends on: Phase 11

## Goal
Typing, shortcuts and scrolling feel native in every direction.

## Tasks
- [ ] Complete HID ↔ platform key tables (ISO and JIS extra keys, numpad, F13–F24, media keys) with layouts applied by the target (Turkish Q and F, US, German verified).
- [ ] Modifier mapping across platforms with a default chosen by the user at the start of the phase (for example ⌘ acting as Ctrl on a PC), changeable per direction.
- [ ] Caps Lock state sync, key repeat forwarding, dead keys through the target's layout.
- [ ] Scroll normalisation: the controller's scroll direction preserved, high-resolution and continuous scroll, momentum.
- [ ] Crossing settings (push time, immediate crossing, crossing only with a modifier) and per-computer pointer speed.

## Acceptance Criteria
- [ ] A documented shortcut matrix passes in all four directions.
- [ ] ğ ü ş ı ö ç Ğ Ü Ş İ Ö Ç type correctly when both computers use a Turkish layout.
- [ ] Scrolling on a remote computer moves content the same way as on the controller.
- [ ] Every table entry round-trips in unit tests.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decision (2026-11-04): stacked on `feature/phase-11-input`; core and Mac now, Windows
  tables verified through CI only, hardware rows wait for the devices.
- User decision (2026-11-04): between a Mac and a PC, shortcuts keep their place by default —
  Mac → PC: ⌘ → Ctrl, ⌃ → Windows, ⌥ → Alt; PC → Mac: Ctrl → ⌘, Windows → ⌃, Alt → ⌥. Mac ↔ Mac
  and PC ↔ PC pass keys unchanged. Changeable per direction. Rejected: keys by position.
