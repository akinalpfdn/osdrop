# Phase 001 (MVP-01) — Repository scaffolding
Status: ACTIVE

DEVPLAN reference: Phase 0. Depends on: Nothing

## Goal
A clean, empty-but-building monorepo with all hygiene, tooling and CI in place.

## Tasks
- [ ] Directory layout as in Platform & Stack.
- [ ] `README.md` skeleton, `LICENSE` (MIT), `CONTRIBUTING.md`, `SECURITY.md` (GitHub private vulnerability reporting), `CODE_OF_CONDUCT.md`, issue and PR templates, `CODEOWNERS`, Dependabot (grouped minor/patch updates, actions pinned to commit SHAs).
- [ ] `.gitignore` (Claude Code files ignored except phase files, as in Osplay), `.gitattributes`, `.editorconfig`.
- [ ] Formatting and linting adapted from Osplay: `.clang-format`, `.clang-tidy`, `.swift-format`, `Directory.Build.props` (nullable, warnings as errors, analyzers), `scripts/lint.sh`, `scripts/format.sh`, clang-tidy runner, localization and UI-token checks; tool versions pinned.
- [ ] `core/` CMake project with an empty library and one passing test; root `Package.swift` exposing `core` and `CoreBridge`.
- [ ] `macos/project.yml` generating an empty menu bar app that links `core`, launches and shows its version.
- [ ] `windows/` CMake presets (MSVC + `portable`) for engine DLL, input process, service and shared library (empty targets that build); a WPF shell solution with a separate localization assembly that launches to a tray icon; `scripts/windows/Enter-DevShell.ps1`.
- [ ] GitHub Actions: macOS job (core, portable Windows libraries, lint, Swift package, app build and tests) and Windows job (core, engine, input, service, shell, tests).
- [ ] `docs/` placeholders: `ARCHITECTURE.md`, `PROTOCOL.md`, `BUILDING.md`, `TESTING.md`, `TROUBLESHOOTING.md`, `SECURITY_MODEL.md`, `THIRD_PARTY.md`, `RELEASING.md`.
- [ ] Repository rulesets as in Osplay: `main` protection and `v*` tag protection.

## Acceptance Criteria
- [ ] A fresh clone builds on macOS (terminal) and Windows (command line) by following `BUILDING.md` exactly.
- [ ] Both CI jobs are green.
- [ ] Every formatter, linter and checker fails CI on an injected violation (shown once per checker).
- [ ] Rulesets are active on `main` and `v*` tags.
- [ ] Tests — One `core` unit test, one Swift package test, one app test, one C# test run in CI.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
