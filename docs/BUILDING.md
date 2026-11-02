# Building Osdrop

Every command on this page is run by CI on each pull request. If a step here fails on a fresh
clone, that is a bug — please open an issue.

| Part | Built on | Toolchain |
|------|----------|-----------|
| `core/`, `windows/shared/` | macOS and Windows | CMake ≥ 3.28, Ninja, Clang or MSVC (C++20) |
| `macos/` | macOS | Xcode 16 or later (Swift 6), XcodeGen |
| `windows/engine`, `windows/input`, `windows/service` | Windows | Visual Studio 2022 or later (MSVC), CMake, Ninja |
| `windows/app` | Windows | .NET 10 SDK |

## macOS

### Requirements

- macOS 14 Sonoma or later.
- Xcode 16 or later, opened once so it finishes installing components.
- [Homebrew](https://brew.sh).

Install the build tools in a terminal:

```sh
brew install cmake ninja xcodegen uv
```

### Build and test

In a terminal:

```sh
git clone https://github.com/akinalpfdn/osdrop.git
cd osdrop

# Shared core (CMake build + GoogleTest)
cd core
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
cd ..

# Windows libraries that build on every host (GoogleTest)
cd windows
cmake --preset portable
cmake --build --preset portable
ctest --preset portable
cd ..

# Shared core as a Swift package (Swift tests)
swift test

# Menu bar app
cd macos
xcodegen generate
xcodebuild -project Osdrop.xcodeproj -scheme Osdrop -destination 'platform=macOS' \
  -derivedDataPath DerivedData test
open DerivedData/Build/Products/Debug/Osdrop.app
```

The app has no Dock icon; it appears in the menu bar as two half arrows crossing a vertical line. Its menu shows the app and
core versions and a Quit button. The app bundle also carries `OsdropCaptureGuard` in
`Contents/MacOS`, built by the same scheme.

Osdrop reads and sends input, so it asks for Accessibility access (System Settings › Privacy &
Security › Accessibility); the menu shows a button that opens the right pane. Debug builds have a
**Developer…** menu item with the input tools of Phase 1. Tests that move the real cursor are in a
separate scheme and never run in CI; see [`TESTING.md`](TESTING.md) → Hardware tests.

To work in Xcode, run `xcodegen generate` in `macos/` and open `Osdrop.xcodeproj`. The project is
generated from `macos/project.yml` and is not committed: re-run `xcodegen generate` after pulling
changes or adding files.

### Code signing for local development

The committed configuration signs ad hoc, so any clone builds without an Apple account. macOS
binds privacy permissions such as Accessibility to the code signature, and ad hoc signatures change
on every build, so regular development needs a stable identity. Create
`macos/Config/Signing.local.xcconfig` (it is gitignored):

```
CODE_SIGN_STYLE = Automatic
CODE_SIGN_IDENTITY = Apple Development
DEVELOPMENT_TEAM = <your team ID>
```

Then run `xcodegen generate` again. Maintainers who also publish releases sign Debug builds with
the release identity (`CODE_SIGN_STYLE = Manual`, `CODE_SIGN_IDENTITY = Developer ID Application`),
so a development build and an installed release share the same permissions.

### Formatting and lint

```sh
scripts/format.sh   # apply clang-format and swift-format in place
scripts/lint.sh     # check formatting, run clang-tidy, check UI tokens and translations
```

Besides formatting and clang-tidy (on `core` and `windows/shared`), `lint.sh` fails when a Mac view
or the shell's XAML uses a literal visual value instead of a theme token
(`scripts/check_ui_tokens.sh`), when Swift code uses a localization key that is not in the string
catalog, and when any string lacks its English or Turkish translation
(`scripts/check_localization.py`). `scripts/lint.sh` is exactly what CI runs. Tool versions are
pinned inside it and fetched with `uv` on first use.

## Windows

### Requirements

- Windows 10 22H2 or Windows 11.
- [Visual Studio 2022 or later](https://visualstudio.microsoft.com/) with the
  **Desktop development with C++** workload. It includes MSVC, CMake and Ninja.
- [.NET 10 SDK](https://dotnet.microsoft.com/download/dotnet/10.0).
- [Git for Windows](https://git-scm.com/download/win).
- [uv](https://docs.astral.sh/uv/) for clang-tidy (`winget install astral-sh.uv`).

### Build and test

In a PowerShell command line:

```powershell
git clone https://github.com/akinalpfdn/osdrop.git
cd osdrop

# Allow the repository's helper script for this PowerShell session only.
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
# Load the Visual Studio x64 compiler environment (also safe inside "Developer PowerShell for VS").
. .\scripts\windows\Enter-DevShell.ps1

# Native engine, input process, service and tests
cd windows
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
python ..\scripts\run_clang_tidy.py windows/build/debug core/src windows/

# WPF shell
cd app
dotnet format Osdrop.slnx --verify-no-changes
dotnet build Osdrop.slnx -c Release
dotnet test Osdrop.slnx -c Release --no-build
dotnet run --project Osdrop.App -c Release
```

The shell starts without a window and shows an "O" icon in the notification area; right-click it
to quit. Windows may hide new icons in the overflow menu (the `^` arrow on the taskbar).

Native binaries land in `windows/build/<preset>/bin/`; .NET builds land in `bin/` folders.

### Formatting

`dotnet format` covers C#. C++ and Swift formatting is checked by the macOS CI job; on Windows,
format a C++ file with the pinned version from `scripts/lint.sh`, for example
`uvx clang-format@23.1.3 -i windows\engine\src\capi\engine_capi.cpp`.

## Continuous integration

`.github/workflows/ci.yml` has three jobs:

- **macOS** — core build and tests, portable Windows libraries and tests, `scripts/lint.sh`,
  `swift test`, app build and tests.
- **macOS sanitizers** — core tests under AddressSanitizer with UndefinedBehaviorSanitizer, and
  under ThreadSanitizer.
- **Windows** — core, engine, input process and service with MSVC (`/W4 /WX`), all native tests,
  clang-tidy on core and Windows code, `dotnet format` check, shell build with analyzers as errors
  and shell tests (every string loads in English and Turkish).
