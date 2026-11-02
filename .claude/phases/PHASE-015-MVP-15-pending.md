# Phase 015 (MVP-15) — Drag and drop across computers
Status: PENDING

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
