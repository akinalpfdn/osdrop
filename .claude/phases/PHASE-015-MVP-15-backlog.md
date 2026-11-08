# Phase 015 (MVP-15) — Drag and drop across computers
Status: BACKLOG

DEVPLAN reference: Phase 14. Depends on: Phases 2, 4, 11, 13

## Goal
A file dragged to an edge crosses with the cursor and lands where it is released, in every direction.

## Tasks
- [ ] Drag probe, offer, drop, cancel and result flows wired to the bridges from Phases 2 and 4 and to the transfer engine.
- [ ] Any member can be the source, including a computer that is itself being controlled remotely; drags across several computers (A → B → C).
- [ ] Progress and cancel on both sides; transfers continue after the cursor moves on; a drop on a target that accepts nothing cancels cleanly.

## Acceptance Criteria
- [ ] Mac → PC, PC → Mac, Mac → Mac, PC → PC, a three-computer chain and a drag started on a remotely controlled computer all copy the file where it is dropped.
- [ ] A 10 GB file and a folder tree with Unicode names arrive intact.
- [ ] Losing the network mid-transfer ends with a clear message on both sides and no partial file.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decision (2026-11-07): core and Mac now, stacked on `feature/phase-16-mac-ui`; the PC
  directions wait for Phase 4 (Windows drag bridge), Mac ↔ Mac testing for the second Mac.
- User decision (2026-11-07): in this PR also drags from apps that give promised files (Photos,
  Mail attachments), received at the edge at the push and carried as files (no slide back), and
  transfer progress and cancel on both sides in the menu, with notifications.

## Plan (core and Mac)
1. Protocol: a forwarded DragOffer names the source computer (new `source` Id), so a target
   fetches from the source even when the controller is a third computer.
2. Core `Control`:
   - The platform reports a drag in progress on this computer (items and their local paths) and
     its end. The items are offered to the transfer engine as `Drag` objects for the members.
   - With a button held, an edge is still a wall, but a push against an edge that has a
     neighbour asks the computer under the cursor for its drag: this computer's own state, or
     DragProbe to the endpoint. A DragOffer with a drag lets the push cross: Enter carries the
     drag_id and the controller forwards the DragOffer (source named) to the target.
   - Target: Enter with a drag presses the button, the forwarded offer goes to the platform to
     start a promise drag; its outcome goes to the source (and the controller) as DragResult.
     Moving on to another computer cancels the drag there (A → B → C).
   - Source: DragResult ends the parked drag as a copy (dropped) or slides it back.
3. C API and CoreBridge for the above.
4. Mac source: drag pasteboard read at drag start (file URLs), reported to core; at the result,
   an injected button release over the edge panel ends the source app's drag (accept or refuse).
   Promise drags (Photos) are received at the edge first and cannot slide back.
5. Mac target: `PromiseDragStarter` with a reader that receives each item from the source through
   the transfer engine; an injected press at the entry point starts the drag.
6. Transfers in the menu: progress and cancel, notifications when done or failed (Phase 16 rest).
   Both sides: core reports sending progress for every member fetching an item.

## Status (2026-11-07)
- Done in code, PR #17 (draft): steps 1–7, including promised-file sources and the transfers list
  with progress, cancel and notifications on both sides.
- Hardware: TESTING D1–D11, all need a second computer.
7. Tests over the simulated network (probe, offer, cross, result, chain, remote source, cancel,
   lost peer) and Mac fakes; docs.
