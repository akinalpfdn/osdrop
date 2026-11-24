# Phase 022 (MOBILE-02) — Phone corners on the computers
Status: PENDING — 0.2.0 (user decisions 2026-11-09 and 2026-11-23)

DEVPLAN reference: Phase 21. Depends on: Phases 14, 021

## Goal
Each phone has a corner on each Mac and PC: files dragged into it go to the phone, and files the
phone sends appear there to be dragged out.

## Tasks
- [ ] A corner per phone in Settings › Devices on each computer.
- [ ] While the phone is connected, a drag reaching the corner shows a small box there with the phone's name; files dropped in go to the phone with progress and cancel.
- [ ] Files the phone sends open the box at its corner by themselves; each can be dragged out (Mac file promises, PC OLE drag source); closing the box moves the rest to Downloads.
- [ ] Mac (`EdgeDropTarget`) and PC (`EdgeTargets`) both; English and Turkish.

## Acceptance Criteria
- [ ] Files dragged from Finder and Explorer to the phone's corner arrive on the phone.
- [ ] Photos shared from the phone appear in the box on the chosen computer and can be dragged into Finder, Explorer and a chat app.
- [ ] With the app closed on the phone, the corner shows nothing and takes nothing.
- [ ] Crossing to other computers through the same edge is unaffected away from the corner.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-23 (user): the box sits in a corner chosen per phone, not on an edge in the arrangement; files from the phone appear in the same box.
