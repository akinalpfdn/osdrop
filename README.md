# Osdrop

[![CI](https://github.com/akinalpfdn/osdrop/actions/workflows/ci.yml/badge.svg)](https://github.com/akinalpfdn/osdrop/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Osdrop lets one keyboard and mouse move across several computers on the same local network. Push
the cursor through a screen edge and it continues on the neighbouring computer, with the keyboard.
Drag a file across the edge and it is copied where you drop it. Copy on one computer and paste on
another.

It works between macOS and Windows in every direction: Mac and PC, two Macs, two PCs, and up to
eight computers in one arrangement.

Türkçe: [README.tr.md](README.tr.md)

## What it is and is not

- **Shared keyboard and mouse:** the computer whose mouse and keyboard you are using drives the
  others; each computer keeps its own monitors and its own desktop.
- **Files and clipboard:** dragged files and folders are copied to where they are dropped; text,
  images and files copied on one computer can be pasted on another.
- **Not screen sharing.** No picture of a screen is sent anywhere. For using one computer as an
  extra display of another, see [Osplay](https://github.com/akinalpfdn/osplay).
- **Local network only.** No accounts, no cloud, no relay servers.

## How it works

Paired computers form a group and keep encrypted connections to each other. When the cursor is
pushed through an edge, the computer you are using stops moving its own cursor and sends your
input to the neighbour, which applies it as if its own mouse and keyboard were used. Details:
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Screenshots

Coming with the first release.

## Requirements

| | |
|---|---|
| **Mac** | macOS 14 Sonoma or later |
| **Windows** | Windows 10 22H2 or Windows 11, x64 |
| **Network** | All computers on the same local network, wired or Wi-Fi |

## Installation

### macOS

1. Download [`Osdrop.dmg`](https://github.com/akinalpfdn/osdrop/releases/latest/download/Osdrop.dmg),
   open it and drag Osdrop to Applications.
2. Start Osdrop; it lives in the menu bar.

### Windows

1. Download [`Osdrop-Setup.exe`](https://github.com/akinalpfdn/osdrop/releases/latest/download/Osdrop-Setup.exe)
   and run it.
2. The installer is not code-signed, so SmartScreen may say "Windows protected your PC": choose
   **More info › Run anyway**. Each release lists the installer's SHA-256.
3. The installer adds the app, the Osdrop Input service (which moves the cursor and types for the
   other computers) and a firewall rule. Osdrop lives in the notification area.

## First run

1. **Mac:** allow **Accessibility** (to read and drive the keyboard and mouse) and **Local
   Network** (to find the other computers) when Osdrop asks.
2. On one computer, open Osdrop's menu (Mac) or panel (Windows) and choose **Pair** next to the
   other computer. Both screens show the same six-digit code; confirm it on both.
3. In **Settings › Arrangement**, drag the computers' screens to where they stand on your desk.
4. Push the cursor through the edge toward the other computer.

More computers join the same way, up to eight. Each computer decides in **Settings › Devices**
what every other one may do: control it, send it files, share its clipboard.

### Keyboard shortcuts

| Action | Mac | Windows |
|--------|-----|---------|
| Bring the cursor back to this computer | ⌃⌥⌘⎋ | Ctrl+Alt+Win+Esc |
| Keep the cursor on the computer it is on (press again to release) | ⌃⌥⌘L | Ctrl+Alt+Win+K |

Both can be changed in **Settings › Input**.

## Security

Computers pair once by confirming a 6-digit code on both screens; all traffic is end-to-end
encrypted. Each computer decides for itself which paired computers may control it, send it files
and share its clipboard. See [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md) and
[`SECURITY.md`](SECURITY.md).

## Troubleshooting

See [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md).

## Building from source

See [`docs/BUILDING.md`](docs/BUILDING.md).

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) and the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

[MIT](LICENSE) © 2026 Akinalp Fidan.

## Credits

Osdrop builds on [Monocypher](https://monocypher.org), [Asio](https://think-async.com/Asio/),
[WPF-UI](https://github.com/lepoco/wpfui), [H.NotifyIcon](https://github.com/HavenDV/H.NotifyIcon)
and [CommunityToolkit.Mvvm](https://github.com/CommunityToolkit/dotnet). Full list:
[`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md).
