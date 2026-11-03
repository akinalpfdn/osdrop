# Phase 016 (MVP-16) — Shared clipboard
Status: ACTIVE

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
- Started while only one Mac is at hand (user, 2026-11-03: "devam edebilirsin"); the only phase whose
  dependencies are met. Branch stacked on Phase 13 (`feature/phase-13-transfer`); its PR targets
  that branch and is not merged until the stack is merged in order.
- Spike (2026-11-04, M3 Air, macOS 26): Finder does not paste a file promise from the general
  pasteboard ("nothing to paste"; it never asked for the name). It does paste a `public.file-url`
  provided lazily by a data provider, but macOS asked for that URL 0.03 s after it was written,
  before any paste (Handoff on, no clipboard manager running: Universal Clipboard most likely), so
  "download when pasted" is "download when copied" on such Macs.
- User decision: files copied on another computer are downloaded in the background into a staging
  folder up to a size cap (default 512 MB, a setting in Phase 16) and put on the pasteboard as real
  file URLs once complete; Finder then pastes them at once with its own progress. Copies above the
  cap put no files on the pasteboard and suggest drag and drop.
