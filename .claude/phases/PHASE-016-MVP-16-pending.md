# Phase 016 (MVP-16) — Shared clipboard
Status: PENDING

DEVPLAN reference: Phase 15. Depends on: Phase 13

## Goal
Copy on one computer and paste on another, for text, images and files.

## Tasks
- [ ] Clipboard watchers (pasteboard change count on macOS, format listener on Windows); announcements carry formats only, never content.
- [ ] Lazy proxies on the other members (pasteboard data providers, delayed rendering); latest copy wins.
- [ ] Formats: plain text, RTF, HTML, PNG (TIFF and DIB converted); files through promises and virtual files, with staged URLs where a paste target needs them. Whether Finder pastes file promises from the general pasteboard is verified at the start of the phase and the approach chosen accordingly.
- [ ] Concealed and transient markers respected in both directions (`org.nspasteboard.ConcealedType` and related types; Windows exclusion formats set on proxied sensitive data).
- [ ] Size caps, the per-device clipboard permission, behaviour with clipboard history verified on Windows and documented.

## Acceptance Criteria
- [ ] Text, rich text, HTML and images paste correctly in all four directions.
- [ ] Passwords copied from a password manager never leave the computer.
- [ ] Pasting copied files in Finder and Explorer copies them with progress.
- [ ] If the source computer disappears, a paste returns quickly with nothing pasted and a notice, never a hang.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
