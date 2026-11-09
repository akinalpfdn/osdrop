# Phase 022 (MOBILE-02) — Corner drop zones on the computers
Status: BACKLOG — after 1.0 (user decision 2026-11-09: phones as drop targets, the app open on the phone)

DEVPLAN reference: Phase 21. Depends on: Phases 14, 20

## Goal
A file dragged to a corner of a Mac or PC lands on a phone, as in Dropover.

## Tasks
- [ ] A corner per phone in Settings › Devices; a drop target there during drags, only while the phone is connected.
- [ ] A small panel at the corner on drop: the phone's name, the files, progress and cancel; files through the transfer engine.
- [ ] Mac (`EdgeDropTarget`) and PC (`EdgeTargets`) both.

## Acceptance Criteria
- [ ] Files dragged from Finder and Explorer to the phone's corner arrive on the phone.
- [ ] With the app closed on the phone, the corner takes nothing and shows nothing.
- [ ] Crossing to other computers through the same edge is unaffected away from the corner.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-09 (user): the phone app must be open on the phone; with it closed, the computers show
  no corner target. No background service.
