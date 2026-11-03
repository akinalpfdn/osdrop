# Phase 014 (MVP-14) — File transfer engine
Status: ACTIVE

DEVPLAN reference: Phase 13. Depends on: Phase 7, Phase 8

## Goal
`core` moves files and folder trees between members quickly, safely and verifiably.

## Tasks
- [x] Manifests (tree, sizes, times, attributes), ranged reads, per-file BLAKE2b verification, cancellation from either side, a concurrency limit and backpressure.
- [x] Portable name mapping in `core` (NFC, Windows-invalid characters, reserved names, trailing dots and spaces, case collisions within one tree, path length). Phase 4 has not run yet (PC away), so there is no earlier function to replace; Phase 4 uses this one.
- [x] Platform readers and writers (long paths on Windows), a free-space check before writing, removal of partial files on failure.
- [x] Marking of received files: none (user decision).
- [x] Symbolic links inside folders: recreated within their item, others skipped (user decision).
- [x] Receiver-side enforcement of the "files" permission.
- [x] C API (offer, withdraw, receive, cancel; offer_ready, transfer_progress, transfer_finished) and the CoreBridge wrapper; the apps start transfers in Phase 14.

## Acceptance Criteria
- [ ] A 10 GB file between a Mac and a PC on wired gigabit reaches at least 90 MB/s. Needs the PC (TESTING F1, F2); loopback on the M3 Air, release build: 1 GiB at 210 MB/s.
- [x] Injected corruption is detected and the partial file removed; cancelling mid-file leaves nothing behind.
- [x] The name corpus (Unicode, emoji, reserved names, long paths) maps losslessly where the target allows and predictably where it does not.
- [x] Manifest and frame decoders are fuzzed.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decision: received files are not marked (no quarantine on macOS, no Mark of the Web on
  Windows).
- User decision: symbolic links inside folders are recreated when their target stays inside the
  transferred tree; others are skipped; on Windows a link that cannot be created is skipped and
  reported.
- Branch stacked on Phase 10 (`feature/phase-10-discovery`); its PR targets that branch and is not
  merged until the stack is merged in order (user, 2026-11-03).
- The 10 GB Mac ↔ PC throughput criterion needs the PC; until then it is measured on loopback.
- File work runs on a file thread; a receiver pauses its stream (new `pause_reading` in the transport) while its disk catches up (DECISIONS.md).
- A failure or cancellation removes everything the transfer created, not only the partial file (DECISIONS.md).
- The sender checks size and time against the scan before ContentEnd (DECISIONS.md).
