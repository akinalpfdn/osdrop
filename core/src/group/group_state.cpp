#include "group/group_state.hpp"

#include "protocol/byte_io.hpp"
#include "protocol/constants.hpp"

#include <array>
#include <string>

namespace osdrop::group {

namespace {

// "ODGS", version, then the fields below (all little-endian):
//   group_id bytes[16]
//   pairing_count u16, pairing × count: device_id[16] public_key[32] epoch u32 platform u8 name
//   revocation_count u16, revocation × count: device_id[16] epoch u32
//   permission_count u16, permission × count: device_id[16] flags u8 (bit 0 control, 1 files,
//   2 clipboard)
constexpr std::array<std::uint8_t, 4> kMagic{'O', 'D', 'G', 'S'};
constexpr std::uint8_t kControl = 1U << 0U;
constexpr std::uint8_t kFiles = 1U << 1U;
constexpr std::uint8_t kClipboard = 1U << 2U;
constexpr std::uint8_t kKnownFlags = kControl | kFiles | kClipboard;

using Decoded = Expected<GroupState, Error>;

using Section = Status<Error>;

Section read_pairings(protocol::Reader& reader, GroupState& state) {
    std::uint16_t count = 0;
    if (!reader.u16(count)) {
        return Section::failure(Error::Truncated);
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        Member member;
        std::uint8_t platform = 0;
        if (!protocol::read_array(reader, member.device_id) ||
            !protocol::read_array(reader, member.public_key) || !reader.u32(member.epoch) ||
            !reader.u8(platform) || !protocol::read_string(reader, member.name)) {
            return Section::failure(Error::Truncated);
        }
        member.platform = static_cast<protocol::Platform>(platform);
        if (platform < 1 || platform > 2 || member.epoch == 0 ||
            state.roster.pairings().contains(member.device_id)) {
            return Section::failure(Error::ValueOutOfRange);
        }
        if (const auto name = protocol::check_string(member.name); !name) {
            return Section::failure(name.error());
        }
        if (!state.roster.add(member)) {
            return Section::failure(Error::ValueOutOfRange); // the ID is not the key's
        }
    }
    return Section::ok();
}

Section read_revocations(protocol::Reader& reader, GroupState& state) {
    std::uint16_t count = 0;
    if (!reader.u16(count)) {
        return Section::failure(Error::Truncated);
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        DeviceId device{};
        std::uint32_t epoch = 0;
        if (!protocol::read_array(reader, device) || !reader.u32(epoch)) {
            return Section::failure(Error::Truncated);
        }
        if (state.roster.revocations().contains(device) || !state.roster.revoke(device, epoch)) {
            return Section::failure(Error::ValueOutOfRange);
        }
    }
    return Section::ok();
}

Section read_permissions(protocol::Reader& reader, GroupState& state) {
    std::uint16_t count = 0;
    if (!reader.u16(count)) {
        return Section::failure(Error::Truncated);
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        DeviceId device{};
        std::uint8_t flags = 0;
        if (!protocol::read_array(reader, device) || !reader.u8(flags)) {
            return Section::failure(Error::Truncated);
        }
        if ((flags & static_cast<std::uint8_t>(~kKnownFlags)) != 0 ||
            state.permissions.contains(device)) {
            return Section::failure(Error::ValueOutOfRange);
        }
        state.permissions.emplace(device, Permissions{.control = (flags & kControl) != 0,
                                                      .files = (flags & kFiles) != 0,
                                                      .clipboard = (flags & kClipboard) != 0});
    }
    return Section::ok();
}

std::uint8_t flags_of(const Permissions& permissions) {
    return static_cast<std::uint8_t>((permissions.control ? kControl : 0U) |
                                     (permissions.files ? kFiles : 0U) |
                                     (permissions.clipboard ? kClipboard : 0U));
}

} // namespace

std::uint32_t offered_features(const Permissions& permissions) {
    std::uint32_t features = protocol::kKnownFeatures;
    if (!permissions.control) {
        features &= ~protocol::kFeatureTakesControl;
    }
    if (!permissions.files) {
        features &= ~(protocol::kFeatureDragTarget | protocol::kFeatureFiles);
    }
    if (!permissions.clipboard) {
        features &= ~protocol::kFeatureClipboard;
    }
    return features;
}

Permissions GroupState::permissions_for(const DeviceId& device) const {
    const auto it = permissions.find(device);
    return it == permissions.end() ? Permissions{} : it->second;
}

std::set<DeviceId> GroupState::file_senders() const {
    std::set<DeviceId> senders;
    for (const Member& member : roster.members()) {
        if (permissions_for(member.device_id).files) {
            senders.insert(member.device_id);
        }
    }
    return senders;
}

std::set<DeviceId> GroupState::clipboard_members() const {
    std::set<DeviceId> members;
    for (const Member& member : roster.members()) {
        if (permissions_for(member.device_id).clipboard) {
            members.insert(member.device_id);
        }
    }
    return members;
}

std::vector<std::uint8_t> encode_state(const GroupState& state) {
    std::size_t size = kMagic.size() + 1 + protocol::kIdSize + 6;
    for (const auto& [device, member] : state.roster.pairings()) {
        size += protocol::kIdSize + protocol::kPublicKeySize + 4 + 1 + 1 + member.name.size();
    }
    size += state.roster.revocations().size() * (protocol::kIdSize + 4);
    size += state.permissions.size() * (protocol::kIdSize + 1);

    std::vector<std::uint8_t> bytes(size);
    protocol::Writer writer(bytes);
    writer.bytes(kMagic);
    writer.u8(kGroupStateVersion);
    writer.bytes(state.group_id);
    writer.u16(static_cast<std::uint16_t>(state.roster.pairings().size()));
    for (const auto& [device, member] : state.roster.pairings()) {
        writer.bytes(device);
        writer.bytes(member.public_key);
        writer.u32(member.epoch);
        writer.u8(static_cast<std::uint8_t>(member.platform));
        protocol::write_string(writer, member.name);
    }
    writer.u16(static_cast<std::uint16_t>(state.roster.revocations().size()));
    for (const auto& [device, epoch] : state.roster.revocations()) {
        writer.bytes(device);
        writer.u32(epoch);
    }
    writer.u16(static_cast<std::uint16_t>(state.permissions.size()));
    for (const auto& [device, permissions] : state.permissions) {
        writer.bytes(device);
        writer.u8(flags_of(permissions));
    }
    bytes.resize(writer.size());
    return bytes;
}

Expected<GroupState, Error> decode_state(std::span<const std::uint8_t> bytes) {
    protocol::Reader reader(bytes);
    std::array<std::uint8_t, kMagic.size()> magic{};
    std::uint8_t version = 0;
    if (!protocol::read_array(reader, magic) || !reader.u8(version)) {
        return Decoded::failure(Error::Truncated);
    }
    if (magic != kMagic) {
        return Decoded::failure(Error::BadMagic);
    }
    if (version != kGroupStateVersion) {
        return Decoded::failure(Error::UnsupportedVersion);
    }
    GroupState state;
    if (!protocol::read_array(reader, state.group_id)) {
        return Decoded::failure(Error::Truncated);
    }
    for (const auto read : {read_pairings, read_revocations, read_permissions}) {
        if (const auto section = read(reader, state); !section) {
            return Decoded::failure(section.error());
        }
    }
    if (reader.remaining() != 0) {
        return Decoded::failure(Error::TrailingBytes);
    }
    return state;
}

} // namespace osdrop::group
