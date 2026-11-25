# Phase 022 (MOBILE-02) — Phone corners on the computers
Status: ACTIVE — 0.2.0 (user decisions 2026-11-09 and 2026-11-23)

DEVPLAN reference: Phase 21. Depends on: Phases 14, 021

## Goal
Each phone has a corner on each Mac and PC: files dragged into it go to the phone, and files the
phone sends appear there to be dragged out.

## Tasks
- [x] A corner per phone in Settings › Devices on each computer.
- [x] While the phone is connected, a drag reaching the corner shows a small box there with the phone's name; files dropped in go to the phone with progress and cancel.
- [x] Files the phone sends open the box at its corner by themselves; each can be dragged out (Mac file promises, PC OLE drag source); closing the box moves the rest to Downloads. (Received files land in Downloads from the start; the box drags out copies.)
- [x] Mac (`EdgeDropTarget`) and PC (`EdgeTargets`) both; English and Turkish.

## Acceptance Criteria
- [x] Files dragged from Finder and Explorer to the phone's corner arrive on the phone.
- [x] Photos shared from the phone appear in the box on the chosen computer and can be dragged into Finder, Explorer and a chat app.
- [x] With the app closed on the phone, the corner shows nothing and takes nothing.
- [ ] Crossing to other computers through the same edge is unaffected away from the corner.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-25: on the PC the engine owns each phone's corner target and places the WPF box in physical pixels (DECISIONS.md).
- 2026-11-24 (user): each phone gets a screen and a corner of it, chosen in Settings › Devices; the default is the bottom-right corner of the main screen.
- 2026-11-23 (user): the box sits in a corner chosen per phone, not on an edge in the arrangement; files from the phone appear in the same box.
