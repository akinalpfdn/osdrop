#include "group/roster.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

namespace osdrop::group {

namespace {

/// Of two entries for the same pairing, the one every member keeps: they agree on the key (the
/// device ID is its fingerprint), so only the name or platform can differ, and the smaller wins.
const Member& preferred(const Member& a, const Member& b) {
    return std::tie(a.name, a.platform) <= std::tie(b.name, b.platform) ? a : b;
}

/// Whether `a` replaces `b`: a higher version; at the same version a block over an unblock, then
/// the name and platform that sort first, so every member keeps the same entry.
bool replaces(const Block& a, const Block& b) {
    if (a.version != b.version) {
        return a.version > b.version;
    }
    if (a.blocked != b.blocked) {
        return a.blocked;
    }
    return std::tie(a.name, a.platform) < std::tie(b.name, b.platform);
}

} // namespace

bool Roster::add(const Member& member) {
    if (member.epoch == 0 || crypto::device_id(member.public_key) != member.device_id) {
        return false;
    }
    const auto it = pairings_.find(member.device_id);
    if (it == pairings_.end()) {
        pairings_.emplace(member.device_id, member);
        return true;
    }
    Member& known = it->second;
    if (member.epoch > known.epoch) {
        known = member;
        return true;
    }
    if (member.epoch == known.epoch && member.public_key == known.public_key) {
        const Member chosen = preferred(known, member);
        if (chosen != known) {
            known = chosen;
            return true;
        }
    }
    return false;
}

bool Roster::revoke(const DeviceId& device, std::uint32_t epoch) {
    if (epoch == 0) {
        return false;
    }
    auto [it, inserted] = revocations_.emplace(device, epoch);
    if (inserted) {
        return true;
    }
    if (epoch > it->second) {
        it->second = epoch;
        return true;
    }
    return false;
}

bool Roster::set_block(const DeviceId& device, const Block& block) {
    if (block.version == 0) {
        return false;
    }
    auto [it, inserted] = blocks_.emplace(device, block);
    if (inserted) {
        return true;
    }
    if (replaces(block, it->second)) {
        it->second = block;
        return true;
    }
    return false;
}

void Roster::block(const DeviceId& device, protocol::Platform platform, std::string name) {
    const auto it = blocks_.find(device);
    const std::uint32_t version = it == blocks_.end() ? 1 : it->second.version + 1;
    blocks_[device] =
        Block{.version = version, .blocked = true, .platform = platform, .name = std::move(name)};
}

void Roster::unblock(const DeviceId& device) {
    const auto it = blocks_.find(device);
    if (it != blocks_.end() && it->second.blocked) {
        ++it->second.version;
        it->second.blocked = false;
    }
}

bool Roster::merge(const Roster& other) {
    bool changed = false;
    for (const auto& [device, member] : other.pairings_) {
        changed = add(member) || changed;
    }
    for (const auto& [device, epoch] : other.revocations_) {
        changed = revoke(device, epoch) || changed;
    }
    for (const auto& [device, block] : other.blocks_) {
        changed = set_block(device, block) || changed;
    }
    return changed;
}

bool Roster::is_blocked(const DeviceId& device) const {
    const auto it = blocks_.find(device);
    return it != blocks_.end() && it->second.blocked;
}

Roster Roster::blocks_without(const DeviceId& except) const {
    Roster kept;
    kept.blocks_ = blocks_;
    kept.blocks_.erase(except);
    return kept;
}

bool Roster::is_member(const DeviceId& device) const {
    const auto pairing = pairings_.find(device);
    if (pairing == pairings_.end() || is_blocked(device)) {
        return false;
    }
    const auto revocation = revocations_.find(device);
    return revocation == revocations_.end() || pairing->second.epoch > revocation->second;
}

std::optional<Member> Roster::member(const DeviceId& device) const {
    if (!is_member(device)) {
        return std::nullopt;
    }
    return pairings_.at(device);
}

std::vector<Member> Roster::members() const {
    std::vector<Member> current;
    for (const auto& [device, member] : pairings_) {
        if (is_member(device)) {
            current.push_back(member);
        }
    }
    return current;
}

std::size_t Roster::size() const {
    return static_cast<std::size_t>(std::ranges::count_if(
        pairings_, [this](const auto& entry) { return is_member(entry.first); }));
}

std::uint32_t Roster::next_epoch(const DeviceId& device) const {
    std::uint32_t newest = 0;
    if (const auto it = pairings_.find(device); it != pairings_.end()) {
        newest = it->second.epoch;
    }
    if (const auto it = revocations_.find(device); it != revocations_.end()) {
        newest = std::max(newest, it->second);
    }
    return newest + 1;
}

std::vector<protocol::RosterState> Roster::to_messages(const protocol::Id& group) const {
    std::vector<protocol::RosterMember> members;
    members.reserve(pairings_.size());
    for (const auto& [device, member] : pairings_) {
        members.push_back({.device_id = device,
                           .public_key = member.public_key,
                           .epoch = member.epoch,
                           .platform = member.platform,
                           .name = member.name});
    }
    std::vector<protocol::Revocation> revoked;
    revoked.reserve(revocations_.size());
    for (const auto& [device, epoch] : revocations_) {
        revoked.push_back({.device_id = device, .epoch = epoch});
    }
    std::vector<protocol::RosterBlock> blocked;
    blocked.reserve(blocks_.size());
    for (const auto& [device, block] : blocks_) {
        blocked.push_back({.device_id = device,
                           .version = block.version,
                           .blocked = block.blocked,
                           .platform = block.platform,
                           .name = block.name});
    }
    if (members.empty()) {
        return {};
    }
    // Every message needs one member at least; the union of all of them is the roster.
    const auto chunks = [](std::size_t size, std::size_t limit) {
        return (size + limit - 1) / limit;
    };
    const auto chunk = []<typename T>(const std::vector<T>& all, std::size_t index,
                                      std::size_t limit) {
        const std::size_t first = std::min(all.size(), index * limit);
        const std::size_t last = std::min(all.size(), (index + 1) * limit);
        return std::vector<T>(all.begin() + static_cast<std::ptrdiff_t>(first),
                              all.begin() + static_cast<std::ptrdiff_t>(last));
    };
    const std::size_t count = std::max({chunks(members.size(), protocol::kMaxMembers),
                                        chunks(revoked.size(), protocol::kMaxRevocations),
                                        chunks(blocked.size(), protocol::kMaxBlocks)});
    std::vector<protocol::RosterState> messages;
    for (std::size_t i = 0; i < count; ++i) {
        protocol::RosterState message{.group_id = group,
                                      .members = chunk(members, i, protocol::kMaxMembers),
                                      .revocations = chunk(revoked, i, protocol::kMaxRevocations),
                                      .blocks = chunk(blocked, i, protocol::kMaxBlocks)};
        if (message.members.empty()) {
            message.members.push_back(members.front());
        }
        messages.push_back(std::move(message));
    }
    return messages;
}

Expected<Roster, Error> Roster::from_message(const protocol::RosterState& state) {
    Roster roster;
    for (const protocol::RosterMember& entry : state.members) {
        const Member member{.device_id = entry.device_id,
                            .public_key = entry.public_key,
                            .epoch = entry.epoch,
                            .platform = entry.platform,
                            .name = entry.name};
        if (crypto::device_id(member.public_key) != member.device_id) {
            return Expected<Roster, Error>::failure(Error::ValueOutOfRange);
        }
        roster.add(member);
    }
    for (const protocol::Revocation& revocation : state.revocations) {
        roster.revoke(revocation.device_id, revocation.epoch);
    }
    for (const protocol::RosterBlock& block : state.blocks) {
        roster.set_block(block.device_id, Block{.version = block.version,
                                                .blocked = block.blocked,
                                                .platform = block.platform,
                                                .name = block.name});
    }
    return roster;
}

} // namespace osdrop::group
