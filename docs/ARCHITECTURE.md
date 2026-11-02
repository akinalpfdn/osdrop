# Architecture

> Status: outline. Sections are filled in by the phases that build each part; the full design is in
> [`DEVPLAN.md`](../DEVPLAN.md) → Architecture.

## Overview

Paired computers form a group with one shared arrangement and keep a full mesh of encrypted
sessions. The computer whose mouse and keyboard are in use is the **controller**; it decides with
the shared crossing engine in `core` which computer receives input. The others are **endpoints**
that inject what they receive.

```
CONTROLLER                                                     ENDPOINT
Event tap / hooks ─► capture ─► core: crossing engine ─┬─UDP motion (latest wins)─► injector
                                                       └─TCP keys, buttons, scroll─►
                                                     ◄──TCP cursor reports, refusals, takeover
```

## Repository layout

| Path | Contents |
|------|----------|
| `core/` | Shared C++20 library with a flat C API (`core/include/osdrop/osdrop.h`) |
| `Package.swift` | Exposes `core` to Swift as `OsdropCore` and its wrapper `CoreBridge` |
| `macos/` | Swift menu bar app (generated with XcodeGen from `macos/project.yml`) and `CoreBridge` |
| `windows/engine/` | Native engine DLL (`OsdropEngine.dll`) used by the shell |
| `windows/input/` | `OsdropInput.exe`: input capture and injection as SYSTEM in the user's session |
| `windows/service/` | `OsdropService`: keeps `OsdropInput` running in the active session |
| `windows/shared/` | Windows-header-free libraries, built and tested on every host |
| `windows/app/` | WPF shell (`Osdrop.App`) and its strings (`Osdrop.Localization`) |
| `windows/installer/` | Inno Setup script |
| `protocol/` | Binary fixtures shared by every test suite |
| `docs/` | This documentation |

## Build graph

- `core` is a static library. CMake (`core/CMakeLists.txt`) is the reference build; SwiftPM
  (`Package.swift`) compiles the same sources for the macOS app.
- The macOS app links `core` through the `CoreBridge` product of the local Swift package.
- `windows/CMakeLists.txt` adds `core` and `windows/shared` and, on Windows, the engine, input
  process and service. On other hosts it builds the portable parts only (`portable` preset).
- The WPF shell loads the engine DLL through P/Invoke.

## macOS input layer

Built in Phase 1 (`macos/Osdrop/Services/Input`):

- **Capture** — `EventTapCapture` runs an active event tap at the HID level on its own thread. The
  callback translates each event into a platform-neutral `InputEvent` (USB HID usages for keys) and
  enqueues it; in capture mode it also swallows the event. Events Osdrop posts carry a tag in their
  source user data and always pass.
- **Freezing and hiding the cursor** — `CGAssociateMouseAndMouseCursorPosition` and
  `BackgroundCursorHider` (the private `SetsCursorInBackground` property, looked up at run time).
- **Ways out of a capture** — the release chord ⌃⌥⌘⎋ (handled in the tap callback); the in-app
  watchdog, which ends a capture whose owner stopped confirming it for 1 s or whose tap stayed
  disabled; and `OsdropCaptureGuard`, a separate process that ends Osdrop after 2 s without a
  heartbeat, because a hung app cannot undo its own frozen, hidden cursor.
- **Injection** — `CGEventInjector` posts CoreGraphics events with click counts, explicit modifier
  flags and a ledger of held keys and buttons, which `releaseAll` lets go of on every way out.
- **Focus** — `InvisibleKeyWindowFocus` takes keyboard focus during capture so password fields in
  other apps resign and Secure Event Input turns off; `SystemSecureInput` names the app that still
  holds it.
