# Osdrop

## What This Is
Osdrop moves one keyboard and mouse across several computers on the same LAN: push the cursor
through a screen edge and it continues on the neighbouring computer. Files dragged across the edge
are copied where they are dropped, and the clipboard is shared. Every direction between macOS and
Windows (Mac ↔ PC, Mac ↔ Mac, PC ↔ PC), up to 8 computers in one arrangement. Not screen sharing
(that is the sibling project Osplay) and not remote access over the internet. Open source (MIT),
no backend services.

`DEVPLAN.md` is the authoritative plan: stack, constraints, protocol decisions and phases.

## Tech Stack
| Layer | Technology | Why |
|-------|-----------|-----|
| Shared core | C++20, CMake + SwiftPM, flat C API | One implementation of protocol, crossings, roster merges and file name mapping |
| macOS app | Swift 6 (strict concurrency), SwiftUI menu bar, XcodeGen | Event taps, event posting, drag sessions, file promises |
| Windows engine | C++20 DLL, CMake presets (Ninja + MSVC) | Sessions, OLE drag and drop, clipboard, files, discovery |
| Windows input | C++20 `OsdropInput.exe` (SYSTEM in the user session) + `OsdropService` | Low-level hooks and SendInput that also work over elevated windows |
| Windows shell | C# .NET 10, WPF, WPF-UI, CommunityToolkit.Mvvm | Tray panel, settings, arrangement; calls the engine via P/Invoke |
| Persistence | Keychain / DPAPI, UserDefaults / JSON | No server, no database |

## Architecture
See `DEVPLAN.md` → Architecture and `docs/ARCHITECTURE.md`.
`core/` (protocol, crypto, transport, session, mesh, group, layout, input, clipboard, transfer, capi)
is used by `macos/` (Swift) and `windows/engine/` (C++ DLL). `windows/input/` captures and injects
input as SYSTEM; `windows/service/` keeps it running in the active session. `windows/app/` is the
WPF shell.

## Current Phase
See `.claude/phases/` — always check the active phase file before starting work.
Phase files map DEVPLAN "Phase N" to `PHASE-0(N+1)-MVP-(N+1)`.

## Rules
claude-ground rules live in `~/.claude/rules/` and load automatically — no imports needed here.
- `common/` rules load in every session.
- Language rules and `frontend.md` load when Claude reads a matching file (`paths:` frontmatter).

## Project-Specific Constraints
- Production grade from the first phase: no throwaway prototypes or spikes in the codebase (user
  decision 2026-11-02). Platform risks are retired by the go/no-go phases 1–4.
- Public repository `akinalpfdn/osdrop`. Day-to-day work is committed and pushed straight to
  `dev`: no feature branches, no PRs, no CI (user decision, 2026-11-10). `dev` reaches `main`
  through a PR when a release is prepared; CI runs there and on `main`.
- Work is tracked only in `.claude/phases/` and `DECISIONS.md`, not in Lore.
- Identifier prefix: `com.akinalpfdn.osdrop`. Bonjour service type `_osdrop._tcp`.
- Code taken from Osplay is copied at a recorded commit and owned here; security fixes in either
  project are checked against the other.
- Input safety invariants (DEVPLAN → Implementation Guidelines) override feature work.
- Never log keystrokes, clipboard contents, file contents or key material.
- Swift 6 isolation: a callback that AppKit or Foundation may run off the main thread (file promise
  delegates and readers, file coordination, operation queues) must be `nonisolated` or `@Sendable`,
  never inherit the main actor from its enclosing type, and needs a test that calls it from a
  background queue. Two such callbacks crashed Osdrop in Phase 2.
- Every user-facing string is localized (English + Turkish); `core`, the engine and the input
  process return error codes, never text.
- Every visual value comes from the platform theme source.
- Docs and READMEs use a plain tone: no emoji, no marketing headings.
- Before pushing to `dev`: clang-format and the tests of what changed. `scripts/lint.sh` (with
  clang-tidy) runs before the PR into `main`, where CI fails on formatting and lint violations.
- Builds and checks are described in `docs/BUILDING.md` — keep it exact; it is an acceptance criterion.

## Context
- Developer hardware: MacBook Air M3 (macOS 26) with an external monitor, a second Mac, a Windows PC
  with two monitors and a second Windows computer; tests run wired and over Wi-Fi.
- Windows code is checked on the Mac with zig's mingw-w64 headers, then built and tested on the PC
  by hand; CI's `windows` job checks it again on the PR into `main`.
- Claude builds and relaunches the Mac app itself; signing uses the maintainer's Developer ID so TCC
  grants (Accessibility) survive rebuilds.
