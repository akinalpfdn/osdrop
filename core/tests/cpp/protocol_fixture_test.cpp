#include "fixtures.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/input_messages.hpp"
#include "protocol/transfer_messages.hpp"

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using osdrop::test::Fixture;
namespace protocol = osdrop::protocol;

/// Decodes with the codec for `kind`, then re-encodes a valid result.
struct Outcome {
    bool valid = false;
    int detail = 0; // type or error
    std::vector<std::uint8_t> reencoded;
};

template <typename Decoded, typename TypeOf, typename Encode>
Outcome run(const Decoded& decoded, TypeOf type_of, Encode encode) {
    if (!decoded) {
        return {.detail = static_cast<int>(decoded.error())};
    }
    const auto encoded = encode(decoded.value());
    EXPECT_TRUE(encoded.has_value());
    return {.valid = true,
            .detail = static_cast<int>(type_of(decoded.value())),
            .reencoded = encoded ? encoded.value() : std::vector<std::uint8_t>{}};
}

Outcome decode(const Fixture& fixture, const std::vector<std::uint8_t>& bytes) {
    if (fixture.kind == "control") {
        return run(
            protocol::decode_control(bytes),
            [](const protocol::ControlMessage& m) { return protocol::type_of(m); },
            protocol::encode_control);
    }
    if (fixture.kind == "input") {
        return run(
            protocol::decode_input(bytes),
            [](const protocol::InputDatagram& d) { return protocol::type_of(d.message); },
            protocol::encode_input);
    }
    EXPECT_EQ(fixture.kind, "transfer");
    return run(
        protocol::decode_transfer(bytes),
        [](const protocol::TransferMessage& m) { return protocol::type_of(m); },
        protocol::encode_transfer);
}

// Every fixture reaches the outcome the manifest promises; valid ones re-encode byte for byte.
TEST(ProtocolFixtures, EveryFixtureMatchesTheManifest) {
    const auto fixtures = osdrop::test::load_manifest();
    ASSERT_FALSE(fixtures.empty());
    for (const Fixture& fixture : fixtures) {
        SCOPED_TRACE(fixture.file + " — " + fixture.description);
        const auto bytes = osdrop::test::read_fixture(fixture.file);
        const Outcome outcome = decode(fixture, bytes);
        ASSERT_EQ(outcome.valid, fixture.valid);
        EXPECT_EQ(outcome.detail, fixture.detail);
        if (fixture.valid) {
            EXPECT_EQ(outcome.reencoded, bytes);
        }
    }
}

TEST(ProtocolFixtures, CoverEveryMessageType) {
    std::map<std::string, std::set<int>> types;
    for (const Fixture& fixture : osdrop::test::load_manifest()) {
        if (fixture.valid) {
            types[fixture.kind].insert(fixture.detail);
        }
    }
    const std::map<std::string, std::vector<int>> expected{
        {"control", {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12,
                     0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x30, 0x31, 0x32, 0x40}},
        {"input", {0x01, 0x02}},
        {"transfer", {0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88}},
    };
    ASSERT_EQ(expected.at("control").size(), std::variant_size_v<protocol::ControlMessage>);
    ASSERT_EQ(expected.at("input").size(), std::variant_size_v<protocol::InputMessage>);
    ASSERT_EQ(expected.at("transfer").size(), std::variant_size_v<protocol::TransferMessage>);
    for (const auto& [kind, list] : expected) {
        for (const int type : list) {
            EXPECT_TRUE(types[kind].contains(type))
                << "no valid " << kind << " fixture for " << type;
        }
    }
}

TEST(ProtocolFixtures, CoverEveryDecodingErrorOfEveryKind) {
    std::map<std::string, std::set<int>> errors;
    for (const Fixture& fixture : osdrop::test::load_manifest()) {
        if (!fixture.valid) {
            errors[fixture.kind].insert(fixture.detail);
        }
    }
    for (const std::string kind : {"control", "input", "transfer"}) {
        for (const auto error : {protocol::Error::Truncated, protocol::Error::BadMagic,
                                 protocol::Error::UnsupportedVersion, protocol::Error::UnknownType,
                                 protocol::Error::TrailingBytes, protocol::Error::ValueOutOfRange,
                                 protocol::Error::TooLarge}) {
            EXPECT_TRUE(errors[kind].contains(static_cast<int>(error)))
                << "no " << kind << " fixture for error " << static_cast<int>(error);
        }
    }
    EXPECT_TRUE(errors["control"].contains(static_cast<int>(protocol::Error::InvalidString)));
    EXPECT_TRUE(errors["transfer"].contains(static_cast<int>(protocol::Error::InvalidString)));
}

} // namespace
