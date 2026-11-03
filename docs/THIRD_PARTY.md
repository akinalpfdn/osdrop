# Third-party components

Licenses were reviewed on 2026-11-02. Each component is re-checked when it is first added to the
build (the phase is listed) and whenever its pinned version changes.

| Component | Used for | License | Distributed with Osdrop | Added in |
|-----------|----------|---------|-------------------------|----------|
| [Monocypher](https://monocypher.org) 4 | X25519, ChaCha20-Poly1305, BLAKE2b | BSD-2-Clause or CC0-1.0 | Yes — vendored source | Phase 6 |
| [Asio](https://think-async.com/Asio/) (standalone) | Sockets and timers in `core` | BSL-1.0 | Yes — vendored headers | Phase 7 |
| [WPF-UI](https://github.com/lepoco/wpfui) | Fluent controls, theme following | MIT | Yes — NuGet assembly | Phase 17 |
| [H.NotifyIcon.Wpf](https://github.com/HavenDV/H.NotifyIcon) 2.4.1 | Windows tray icon | MIT | Yes — NuGet assembly | Phase 0 |
| [CommunityToolkit.Mvvm](https://github.com/CommunityToolkit/dotnet) 8.4.2 | WPF view models | MIT | Yes — NuGet assembly | Phase 0 |
| [GoogleTest](https://github.com/google/googletest) 1.18.0 | C++ tests | BSD-3-Clause | No — build time only | Phase 0 |
| [xUnit.net](https://xunit.net) v3 | C# tests | Apache-2.0 | No — build time only | Phase 0 |
| [XcodeGen](https://github.com/yonaskolb/XcodeGen) | Xcode project generation | MIT | No — build tool | Phase 0 |
| [Inno Setup](https://jrsoftware.org/isinfo.php) | Windows installer | Inno Setup License | Installer runtime only | Phase 19 |

Release packages must include each distributed component's copyright and license text: the macOS
app in its acknowledgements resource, the Windows installer on its license page and in the install
folder.

## Code from Osplay

Parts of [Osplay](https://github.com/akinalpfdn/osplay)'s core (MIT, same author) are copied into
`core/` and adapted. Phase 5 copied the protocol utilities (`util/expected.hpp`,
`protocol/byte_io.hpp`, the UTF-8 rules of `protocol/text.cpp`), the codec and fixture layout and
the fuzz replay runner; Phase 6 copies the Noise handshake, cipher states, replay window and
randomness injection, and Phase 7 the Asio transport base and control framing. The Osplay commit
each copy comes from is recorded in `DECISIONS.md`. A security fix in either project is checked
against the other.
