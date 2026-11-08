#pragma once

// What a computer keeps about its group between runs: the group ID, the roster and its own
// permissions for each member. The platforms store the encoded bytes in their secure stores
// (Keychain, DPAPI) next to the identity, because they decide who may connect; core never writes
// files. The format is versioned (docs/ARCHITECTURE.md, "Group state").

#include "group/roster.hpp"
#include "protocol/wire.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <vector>

namespace osdrop::group {

/// What this computer accepts from one member. New members get everything.
struct Permissions {
    bool control = true;   ///< may control this computer
    bool files = true;     ///< may send files to it
    bool clipboard = true; ///< may share clipboards with it, both ways
    bool operator==(const Permissions&) const = default;
};

/// The Capabilities features this computer offers a peer it gives `permissions`.
[[nodiscard]] std::uint32_t offered_features(const Permissions& permissions);

struct GroupState {
    protocol::Id group_id{}; ///< all zeros when in no group
    Roster roster;
    std::map<DeviceId, Permissions> permissions; ///< only entries that differ from the default

    [[nodiscard]] bool in_group() const { return !protocol::is_zero(group_id); }
    [[nodiscard]] Permissions permissions_for(const DeviceId& device) const;
    /// The members this computer accepts files from.
    [[nodiscard]] std::set<DeviceId> file_senders() const;
    /// The members this computer shares clipboards with, both ways.
    [[nodiscard]] std::set<DeviceId> clipboard_members() const;
    /// The members that may control this computer.
    [[nodiscard]] std::set<DeviceId> controllers() const;
    bool operator==(const GroupState&) const = default;
};

inline constexpr std::uint8_t kGroupStateVersion = 1;

[[nodiscard]] std::vector<std::uint8_t> encode_state(const GroupState& state);
/// UnsupportedVersion for a state written by a newer Osdrop: the platform must keep it rather
/// than overwrite it.
[[nodiscard]] Expected<GroupState, Error> decode_state(std::span<const std::uint8_t> bytes);

} // namespace osdrop::group
