# Phase 010 (MVP-10) — Layout and crossing engine
Status: ACTIVE

DEVPLAN reference: Phase 9. Depends on: Phase 8

## Goal
One shared arrangement places every member's monitors in a global space, and `core` decides every crossing, entry point and edge push from it.

## Tasks
- [x] Screen reports and the layout document with last-writer-wins merges and validation (no overlaps; every computer touches the arrangement or is marked detached).
- [x] Crossing geometry: exit detection on edges not covered by the computer's own monitors, neighbour search with snap tolerance, entry mapping, walls where no neighbour exists.
- [x] Edge push state machine (push time and distance, immediate mode), virtual cursor, pointer speed normalisation and per-computer multiplier.
- [x] Conversion to injection coordinates (macOS points, Windows physical pixels per monitor scale).
- [x] `capi`.

## Acceptance Criteria
- [x] Property tests over random arrangements: leaving and re-entering through the same edge returns within one unit; no crossing into gaps; mixed scales, vertical stacks and L-shaped desktops behave like the platforms' own display arrangements.
- [x] Layout merges converge in every order.
- [x] Edge push timing is deterministic with a fake clock.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- ScreenReport carries each monitor's native rectangle too; remote cursors move in native units.
- Core derives Windows logical frames from physical rectangles and scales.
- One layout document, newest (revision, author) wins with a content tie break; members place
  themselves; joining or leaving a group starts over; invalid arrangements are refused when
  published (INVALID_LAYOUT, 31).
- Edge push defaults: 1 s, 32 units, pauses up to 300 ms; immediate mode.
- The crossing engine gets its C API in Phase 11.
