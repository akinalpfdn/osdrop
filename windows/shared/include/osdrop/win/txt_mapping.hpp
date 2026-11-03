#pragma once

// Between DNS-SD key/value lists and core's TXT record (docs/PROTOCOL.md "Discovery"), and the
// service instance names the engine uses. Portable and unit tested.

#include <osdrop/osdrop.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace osdrop::win {

using TxtPairs = std::vector<std::pair<std::string, std::string>>;

/// Every key in a fixed order, ready for registration.
[[nodiscard]] TxtPairs txt_pairs(const osdrop_txt_record& record);

/// A peer's record: known keys copied (cut to the value size), unknown ones ignored, missing ones
/// left empty for core to reject.
[[nodiscard]] osdrop_txt_record txt_record(const TxtPairs& pairs);

/// The instance label: the device ID in hex, unique per computer and stable across renames; the UI
/// shows the TXT name instead.
[[nodiscard]] std::string instance_label(const osdrop_txt_record& record);

/// "label._osdrop._tcp.local" (with or without the final dot) → "label"; nullopt for anything else.
[[nodiscard]] std::optional<std::string> label_of(const std::string& instance_name);

} // namespace osdrop::win
