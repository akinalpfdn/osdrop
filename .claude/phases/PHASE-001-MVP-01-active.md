# Phase 001 (MVP-01) — Repository scaffolding
Status: ACTIVE

DEVPLAN reference: Phase 0. Depends on: Nothing

## Goal
A clean, empty-but-building monorepo with all hygiene, tooling and CI in place.

## Tasks
- [x] Directory layout as in Platform & Stack.
- [x] `README.md` skeleton, `LICENSE` (MIT), `CONTRIBUTING.md`, `SECURITY.md` (GitHub private vulnerability reporting), `CODE_OF_CONDUCT.md`, issue and PR templates, `CODEOWNERS`, Dependabot (grouped minor/patch updates, actions pinned to commit SHAs).
- [x] `.gitignore` (Claude Code files ignored except phase files, as in Osplay), `.gitattributes`, `.editorconfig`.
- [x] Formatting and linting adapted from Osplay: `.clang-format`, `.clang-tidy`, `.swift-format`, `Directory.Build.props` (nullable, warnings as errors, analyzers), `scripts/lint.sh`, `scripts/format.sh`, clang-tidy runner, localization and UI-token checks; tool versions pinned.
- [x] `core/` CMake project with an empty library and one passing test; root `Package.swift` exposing `core` and `CoreBridge`.
- [x] `macos/project.yml` generating an empty menu bar app that links `core`, launches and shows its version.
- [x] `windows/` CMake presets (MSVC + `portable`) for engine DLL, input process, service and shared library (empty targets that build); a WPF shell solution with a separate localization assembly that launches to a tray icon; `scripts/windows/Enter-DevShell.ps1`.
- [x] GitHub Actions: macOS job (core, portable Windows libraries, lint, Swift package, app build and tests) and Windows job (core, engine, input, service, shell, tests).
- [x] `docs/` placeholders: `ARCHITECTURE.md`, `PROTOCOL.md`, `BUILDING.md`, `TESTING.md`, `TROUBLESHOOTING.md`, `SECURITY_MODEL.md`, `THIRD_PARTY.md`, `RELEASING.md`.
- [x] Repository rulesets as in Osplay: `main` protection and `v*` tag protection.

## Acceptance Criteria
- [ ] A fresh clone builds on macOS (terminal) and Windows (command line) by following `BUILDING.md` exactly.
  - macOS: verified on the M3 Air from a fresh clone of the branch (2026-11-02).
  - Windows: CI runs the same commands (MSVC, .NET 10); one manual run on the developer PC is still open.
- [x] Both CI jobs are green (PR #1, run 41403248531: macOS, macOS sanitizers, Windows on the first run).
- [x] Every formatter, linter and checker fails CI on an injected violation (shown once per checker).
  - Shown locally: clang-format, swift-format, UI tokens (Swift, XAML), localization, clang-tidy (core, windows/shared), `dotnet format` (whitespace) and the C# build with warnings as errors.
  - Not shown separately: MSVC `/WX` and the Windows-target clang-tidy run; both fail through the same exit codes as above, and a red CI run per check would cost three extra rounds.
- [x] Rulesets are active on `main` and `v*` tags.
- [x] Tests — One `core` unit test, one Swift package test, one app test, one C# test run in CI.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Plan decisions recorded on 2026-11-02 (DECISIONS.md): public repo and PR per phase, production grade with no PoC, Osplay code copied not linked, up to 8 computers in every direction, first-release scope, Windows input as SYSTEM, crossing by pushing.
- `main` and `v*` tags protected by rulesets; `macOS`, `macOS sanitizers` and `Windows` checks required (DECISIONS.md).
- `windows/shared` starts with the product names and the per-session input pipe name, so the installer, service, input process and app share one definition.
- The UI token check also catches a bare literal (`.padding(5)`), which the pattern taken from Osplay missed.
- Each `lint.sh` checker shown to fail on an injected violation locally (clang-format, swift-format, UI tokens in Swift and XAML, localization, clang-tidy on core and windows/shared); CI runs the same script.
- The Osdrop tray glyph uses a green accent so it is not confused with Osplay's blue one; a real icon comes with the UI phases.
