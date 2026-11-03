#pragma once

// The Bonjour TXT record (docs/PROTOCOL.md, "Discovery"): one implementation of its format and
// checks for both platforms' DNS-SD code.

#include "protocol/control_messages.hpp"
#include "protocol/error.hpp"
#include "protocol/wire.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace osdrop::protocol {

inline constexpr std::string_view kServiceType = "_osdrop._tcp";

struct Advertisement {
    std::uint32_t protocol_version = kVersion;
    std::uint8_t protocol_minor = kMinorVersion;
    Id device_id{};
    std::string name;
    Platform platform = Platform::MacOS;
    std::optional<Id> group_id; ///< empty when the computer is in no group
    bool operator==(const Advertisement&) const = default;
};

/// The TXT values in the document's key order: pv, pm, id, name, os, grp.
struct TxtValues {
    std::string protocol_version;
    std::string protocol_minor;
    std::string device_id;
    std::string name;
    std::string os;
    std::string group_id;
    bool operator==(const TxtValues&) const = default;
};

/// InvalidString for a bad name, ValueOutOfRange for anything else invalid.
[[nodiscard]] Expected<TxtValues, Error> to_txt(const Advertisement& advertisement);

/// Parses a peer's record. A different protocol version is returned, not refused, so the UI can
/// say "update the other computer".
[[nodiscard]] Expected<Advertisement, Error> from_txt(const TxtValues& values);

} // namespace osdrop::protocol
