// Fuzz entry point for the group: the stored group state and RosterState from members. Properties
// checked on any input (run under ASan/UBSan): nothing crashes or reads out of bounds; a decoded
// state encodes canonically and decodes back to itself; and a roster from the network merges into
// a known one the same way in either order, and merging it twice changes nothing.
#include "crypto/noise.hpp"
#include "group/group_state.hpp"
#include "group/roster.hpp"
#include "protocol/control_messages.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <variant>
#include <vector>

namespace {

using osdrop::group::Member;
using osdrop::group::Roster;

void check(bool condition) {
    if (!condition) {
        std::abort();
    }
}

void fuzz_state(std::span<const std::uint8_t> input) {
    const auto decoded = osdrop::group::decode_state(input);
    if (!decoded) {
        return;
    }
    const std::vector<std::uint8_t> encoded = osdrop::group::encode_state(decoded.value());
    const auto again = osdrop::group::decode_state(encoded);
    check(again.has_value() && again.value() == decoded.value());
    check(osdrop::group::encode_state(again.value()) == encoded);
}

constexpr std::uint8_t kRevokedEpoch = 3;

osdrop::crypto::Key key_of(std::uint8_t seed) {
    osdrop::crypto::Key key{};
    key.fill(seed);
    return key;
}

osdrop::group::DeviceId revoked_device() {
    return osdrop::crypto::device_id(key_of(kRevokedEpoch));
}

/// A roster with two members and one revoked computer, for merging into.
Roster known_roster() {
    Roster roster;
    for (std::uint8_t seed = 1; seed <= kRevokedEpoch; ++seed) {
        roster.add(Member{.device_id = osdrop::crypto::device_id(key_of(seed)),
                          .public_key = key_of(seed),
                          .epoch = seed,
                          .platform = osdrop::protocol::Platform::Windows,
                          .name = "Member"});
    }
    roster.revoke(revoked_device(), kRevokedEpoch);
    return roster;
}

void fuzz_roster(std::span<const std::uint8_t> input) {
    const auto message = osdrop::protocol::decode_control(input);
    if (!message) {
        return;
    }
    const auto* state = std::get_if<osdrop::protocol::RosterState>(&message.value());
    if (state == nullptr) {
        return;
    }
    const auto received = Roster::from_message(*state);
    if (!received) {
        return;
    }
    const Roster known = known_roster();
    Roster known_then_received = known;
    known_then_received.merge(received.value());
    Roster received_then_known = received.value();
    received_then_known.merge(known);
    check(known_then_received == received_then_known);
    check(!known_then_received.merge(received.value()));
    // The revoked pairing stays revoked; only a newer pairing brings that computer back.
    const auto revoked = known_then_received.member(revoked_device());
    check(!revoked || revoked->epoch > kRevokedEpoch);
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);
    fuzz_state(input);
    fuzz_roster(input);
    return 0;
}
