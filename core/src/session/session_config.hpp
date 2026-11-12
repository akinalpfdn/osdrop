#pragma once

// Every tunable of a session in one place; the defaults are the shipping values, tests shorten
// them. Timing rules are documented in docs/PROTOCOL.md, "Session flow".

#include "protocol/control_messages.hpp"
#include "util/time.hpp"

#include <string>

namespace osdrop::session {

struct SessionTunables {
    /// Hello and the three handshake messages must complete within this time.
    Microseconds handshake_timeout{10'000'000};
    /// A user who never answers a pairing code ends the attempt after this time.
    Microseconds pairing_timeout{120'000'000};
    /// Heartbeat after this long without sending anything, so the peer can tell silence from
    /// loss quickly.
    Microseconds heartbeat_interval{250'000};
    /// Neither a control message nor an input datagram for this long: the peer is unresponsive,
    /// and input to and from it stops (DEVPLAN: peer loss detected within one second). Datagrams
    /// count because Wi-Fi loses packets: the control connection then waits about a second to
    /// resend one, while the next datagram gets through.
    Microseconds unresponsive_after{750'000};
    /// Nothing received for this long ends the session with TIMED_OUT.
    Microseconds session_timeout{10'000'000};
    Microseconds ping_interval{1'000'000};
    /// InputHello repeats this often until the peer's arrives, then at input_keepalive_interval
    /// to keep the association (and firewall state) alive and tell the peer this side lives.
    Microseconds input_hello_interval{250'000};
    Microseconds input_keepalive_interval{250'000};
    Microseconds stats_interval{1'000'000};
};

struct SessionConfig {
    protocol::Platform platform = protocol::Platform::MacOS;
    std::string device_name;
    protocol::Id group_id{}; ///< all zeros when in no group
    protocol::Capabilities capabilities;
    SessionTunables tunables;
};

} // namespace osdrop::session
