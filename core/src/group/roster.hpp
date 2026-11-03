#pragma once

// The group roster (docs/PROTOCOL.md, "Group membership"): for every device, its newest pairing
// (epoch) and its newest revocation. A device is a member while its newest pairing is newer than
// its newest revocation. Rosters merge by keeping the newer of each, so every member converges
// whatever the order of messages and whoever was offline, and a revocation is undone only by a
// later pairing, never by a member that missed it.

#include "crypto/noise.hpp"
#include "protocol/control_messages.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace osdrop::group {

using DeviceId = protocol::Id;

/// The most computers in one group (DEVPLAN: a full mesh of at most 28 sessions).
inline constexpr std::size_t kMaxGroupSize = 8;

struct Member {
    DeviceId device_id{};
    crypto::Key public_key{};
    std::uint32_t epoch = 1;
    protocol::Platform platform = protocol::Platform::MacOS;
    std::string name;
    bool operator==(const Member&) const = default;
};

class Roster {
public:
    /// Adds a pairing. Returns whether the roster changed; false also for an entry whose device ID
    /// is not the fingerprint of its key, which is never stored.
    bool add(const Member& member);
    /// Revokes a device's pairings up to `epoch`. Returns whether the roster changed.
    bool revoke(const DeviceId& device, std::uint32_t epoch);
    /// Merges another roster in; returns whether this one changed.
    bool merge(const Roster& other);

    [[nodiscard]] bool is_member(const DeviceId& device) const;
    /// The device's pairing while it is a member.
    [[nodiscard]] std::optional<Member> member(const DeviceId& device) const;
    [[nodiscard]] std::vector<Member> members() const;
    [[nodiscard]] std::size_t size() const;
    /// The epoch the device's next pairing gets: newer than anything known about it.
    [[nodiscard]] std::uint32_t next_epoch(const DeviceId& device) const;

    /// Every entry, members and revoked alike, as stored and sent.
    [[nodiscard]] const std::map<DeviceId, Member>& pairings() const { return pairings_; }
    [[nodiscard]] const std::map<DeviceId, std::uint32_t>& revocations() const {
        return revocations_;
    }

    /// The roster as RosterState messages, split to fit the message's limits. Empty for an empty
    /// roster.
    [[nodiscard]] std::vector<protocol::RosterState> to_messages(const protocol::Id& group) const;
    /// The roster in one RosterState; fails when a device ID is not the fingerprint of its key.
    [[nodiscard]] static Expected<Roster, Error> from_message(const protocol::RosterState& state);

    bool operator==(const Roster&) const = default;

private:
    std::map<DeviceId, Member> pairings_;           // newest pairing per device
    std::map<DeviceId, std::uint32_t> revocations_; // newest revoked epoch per device
};

} // namespace osdrop::group
