# Phase 014 (MVP-14) — File transfer engine
Status: PENDING

DEVPLAN reference: Phase 13. Depends on: Phase 7, Phase 8

## Goal
`core` moves files and folder trees between members quickly, safely and verifiably.

## Tasks
- [ ] Manifests (tree, sizes, times, attributes), ranged reads, per-file BLAKE2b verification, cancellation from either side, a concurrency limit and backpressure.
- [ ] Portable name mapping in `core` (NFC, Windows-invalid characters, reserved names, trailing dots and spaces, case collisions within one tree, path length), replacing the Phase 4 function.
- [ ] Platform readers and writers (long paths on Windows), a free-space check before writing, removal of partial files on failure.
- [ ] Marking of received files (quarantine on macOS, Mark of the Web on Windows), decided with the user at the start of the phase.
- [ ] Symbolic links inside folders arrive as link entries whose content is the target; whether a receiver recreates, follows or skips them is decided with the user at the start of the phase.
- [ ] Receiver-side enforcement of the "files" permission.

## Acceptance Criteria
- [ ] A 10 GB file between a Mac and a PC on wired gigabit reaches at least 90 MB/s.
- [ ] Injected corruption is detected and the partial file removed; cancelling mid-file leaves nothing behind.
- [ ] The name corpus (Unicode, emoji, reserved names, long paths) maps losslessly where the target allows and predictably where it does not.
- [ ] Manifest and frame decoders are fuzzed.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
