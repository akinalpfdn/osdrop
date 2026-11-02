# Phase 003 (MVP-03) — macOS drag bridge
Status: ACTIVE

DEVPLAN reference: Phase 2 (go/no-go gate). Depends on: Phase 1

## Goal
On one Mac, a file dragged to a screen edge is captured by Osdrop, and Osdrop can start a promise drag from injected input that drops the file into Finder or an app.

## Tasks
- [ ] `DragCapturing`: drag detection through the drag pasteboard while a button is held; edge drop-target panels along given segments, shown only during a drag; reading file URLs, folders and file promises (received into a staging area); ending the source drag as a copy or rejecting it so it slides back.
- [ ] `DragStarting`: helper window that receives an injected button-down; `NSDraggingSession` with one `NSFilePromiseProvider` per top-level item (folders included); asynchronous writes through a `PromisedContentReading` interface (local file reader now, network reader in Phase 14); collision-free names; Finder progress for destination files; drag image from item icons and count; lazily provided staged file URLs below a size cap.
- [ ] Cancellation: Esc, drops on targets that accept nothing, and source loss end cleanly with no partial files.
- [ ] Debug-only loopback: a drag captured at the right edge reappears as an Osdrop drag at the left edge of the same Mac.

## Acceptance Criteria
- [ ] Loopback copies one file, many files, a folder tree and a 10 GB file into a Finder window and onto the desktop; the large copy shows progress and Finder never stops responding.
- [ ] Dropping into Mail, Messages and a browser upload field works through promises or the staged URL; a target that accepts neither refuses cleanly.
- [ ] Drags from Finder and from Photos (promised files) are captured.
- [ ] The source drag ends as a copy on success and slides back on cancel; the source file is never moved or removed.
- [ ] **If a live drag cannot be captured at the edge, or a drag cannot be started from injected input, stop and document findings before any further phase.**
- [ ] Tests — Unit: name collision resolution, promise writer state machine, staging cleanup with fake file systems.
- [ ] Tests — Hardware: loopback scenarios in the hardware scheme.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
