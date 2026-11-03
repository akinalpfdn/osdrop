#pragma once

// Tells the operating system what kind of traffic a socket carries, so Wi-Fi treats input as
// latency sensitive: on macOS the network service type selects the WMM access category. Osplay
// measured that marking halves Wi-Fi loss caused by AWDL channel hopping (Osplay DECISIONS.md,
// 2026-10-25).

#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>

namespace osdrop::transport {

/// Marks an input socket as interactive voice, the highest priority: pointer motion is tiny and
/// late motion is useless. Best effort: false where the platform has no such option (Windows,
/// Linux) or refuses it.
bool mark_as_interactive_voice(asio::ip::udp::socket& socket);

/// Marks a control connection as signaling (keys, buttons, heartbeats). Best effort as above.
bool mark_as_signaling(asio::ip::tcp::socket& socket);

} // namespace osdrop::transport
