# Phase 005 (MVP-05) — Windows drag bridge
Status: BACKLOG — the go/no-go gate passed on the PC (2026-11-10, DW1–DW3); DW4–DW7 still to run

DEVPLAN reference: Phase 4 (go/no-go gate). Depends on: Phase 3

## Goal
On one PC, a file dragged to a screen edge is captured, and Osdrop can start a virtual-file drag from injected input that drops into Explorer or an app.

## Tasks
- [x] Edge drop targets (topmost, layered, non-activating, STA thread) shown while the left button is held near a crossing edge; reading `CF_HDROP` and virtual files; ending the source drag as a copy or rejecting it.
- [x] Drag start: helper window receiving an injected button-down; `DoDragDrop` on a dedicated STA thread; data object with `CFSTR_FILEDESCRIPTORW` and `CFSTR_FILECONTENTS` (one stream per file, read through `PromisedContentReading`), `IDataObjectAsyncCapability`; delayed `CF_HDROP` to staged copies below a size cap; drag image through `IDragSourceHelper`.
- [x] Name and path mapping used by the descriptors (long paths, reserved names, invalid characters) as a portable function, later moved into `core` in Phase 13.
- [ ] UIPI limits measured and recorded (drags from elevated apps).
- [x] Debug-only loopback as in Phase 2.
- [x] Announce `OSDROP_FEATURE_DRAG_SOURCE` and `OSDROP_FEATURE_DRAG_TARGET` in `windows/shared` (`kAvailableFeatures`) once drags work both ways. (Phase 14)

## Acceptance Criteria
- [ ] Loopback copies one file, many files, a folder tree and a 10 GB file into Explorer and onto the desktop with Explorer's progress dialog, and Explorer never stops responding.
- [ ] Dropping into Outlook, Teams and a browser upload field works through virtual files or staged paths.
- [ ] Drags out of Outlook attachments and zip folders are captured.
- [x] The elevated-source limitation is documented in `docs/TROUBLESHOOTING.md`.
- [x] **If a live drag cannot be captured or started from injected input, stop and document findings before any further phase.** Passed: captured and started.
- [x] Tests — Unit (portable): descriptor building, name mapping corpus, stream read/seek logic with fake readers.
- [ ] Tests — Hardware: loopback scenarios with the developer tool.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-09: virtual files with async capability, CF_HDROP staged up to 64 MB; strips shown while
  the left button is down. Name mapping: local names already fit; names from other computers are
  mapped by core (Phase 13's `map_name`), wired in Phase 14.
- 2026-11-10: PC loopback (Debug, two monitors): a file, a folder and a large file reached Explorer
  with its progress dialog, originals untouched. Strips exist only on outer desktop edges; TESTING
  said "right edge", which is inner on this PC, and now says outer edge.
