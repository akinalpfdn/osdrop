# Phase 016 (MVP-16) — Shared clipboard
Status: ACTIVE

DEVPLAN reference: Phase 15. Depends on: Phase 13

## Goal
Copy on one computer and paste on another, for text, images and files.

## Tasks
- [x] Clipboard watchers (pasteboard change count on macOS, format listener on Windows); announcements carry formats only, never content.
- [x] Copies on the other members fetched in the background and written as real data (user decisions replace lazy proxies); latest copy wins (clip IDs from the wall clock).
- [x] Formats: plain text, RTF, HTML, PNG (TIFF and DIB converted); files staged and placed as file URLs (Mac) and CF_HDROP (PC). Finder spike done at the start (no promise paste).
- [x] Concealed and transient markers respected in both directions: such copies are never read, so they never reach another computer (nothing sensitive is proxied to mark).
- [x] Size caps and the per-device clipboard permission. Windows clipboard history needs the PC (TESTING CL8).

## Acceptance Criteria
(all need two computers: TESTING CL1–CL8; Mac ↔ Mac needs the second Mac, the rest the PC)
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
- User decision: on the Mac, text, RTF, HTML and images copied on another computer are also fetched
  in the background (within size caps) and written to the pasteboard as real data once they arrive,
  instead of lazy data providers: macOS asks for lazy data at once, and a provider would block the
  main thread on the network. Windows keeps delayed rendering (its render requests come on a
  window thread of Osdrop's own).
- User decision: Windows works like the Mac: copies from other computers are fetched in the
  background within the limits and written as real data (text, RTF, HTML, PNG and DIB; files from
  a staging folder as CF_HDROP). Plain Win32, no delayed rendering or OLE virtual files; replaces
  the earlier "Windows keeps delayed rendering".
