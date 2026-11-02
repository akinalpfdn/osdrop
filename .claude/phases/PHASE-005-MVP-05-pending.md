# Phase 005 (MVP-05) — Windows drag bridge
Status: PENDING

DEVPLAN reference: Phase 4 (go/no-go gate). Depends on: Phase 3

## Goal
On one PC, a file dragged to a screen edge is captured, and Osdrop can start a virtual-file drag from injected input that drops into Explorer or an app.

## Tasks
- [ ] Edge drop targets (topmost, layered, non-activating, STA thread) shown while the left button is held near a crossing edge; reading `CF_HDROP` and virtual files; ending the source drag as a copy or rejecting it.
- [ ] Drag start: helper window receiving an injected button-down; `DoDragDrop` on a dedicated STA thread; data object with `CFSTR_FILEDESCRIPTORW` and `CFSTR_FILECONTENTS` (one stream per file, read through `PromisedContentReading`), `IDataObjectAsyncCapability`; delayed `CF_HDROP` to staged copies below a size cap; drag image through `IDragSourceHelper`.
- [ ] Name and path mapping used by the descriptors (long paths, reserved names, invalid characters) as a portable function, later moved into `core` in Phase 13.
- [ ] UIPI limits measured and recorded (drags from elevated apps).
- [ ] Debug-only loopback as in Phase 2.

## Acceptance Criteria
- [ ] Loopback copies one file, many files, a folder tree and a 10 GB file into Explorer and onto the desktop with Explorer's progress dialog, and Explorer never stops responding.
- [ ] Dropping into Outlook, Teams and a browser upload field works through virtual files or staged paths.
- [ ] Drags out of Outlook attachments and zip folders are captured.
- [ ] The elevated-source limitation is documented in `docs/TROUBLESHOOTING.md`.
- [ ] **If a live drag cannot be captured or started from injected input, stop and document findings before any further phase.**
- [ ] Tests — Unit (portable): descriptor building, name mapping corpus, stream read/seek logic with fake readers.
- [ ] Tests — Hardware: loopback scenarios with the developer tool.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
