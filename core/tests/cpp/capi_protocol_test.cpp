#include "fixtures.hpp"

#include <osdrop/osdrop.h>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace {

osdrop_wire_kind kind_of(const osdrop::test::Fixture& fixture) {
    if (fixture.kind == "control") {
        return OSDROP_WIRE_CONTROL_MESSAGE;
    }
    return fixture.kind == "input" ? OSDROP_WIRE_INPUT_DATAGRAM : OSDROP_WIRE_TRANSFER_MESSAGE;
}

TEST(CapiProtocol, ValidateAndReencodeAgreeWithTheManifest) {
    std::vector<std::uint8_t> out(OSDROP_MAX_CONTROL_MESSAGE_SIZE);
    for (const auto& fixture : osdrop::test::load_manifest()) {
        SCOPED_TRACE(fixture.file);
        const auto bytes = osdrop::test::read_fixture(fixture.file);
        std::uint8_t type = 0;
        const osdrop_result validated =
            osdrop_protocol_validate(kind_of(fixture), bytes.data(), bytes.size(), &type);
        if (!fixture.valid) {
            EXPECT_EQ(static_cast<int>(validated), fixture.detail);
            continue;
        }
        ASSERT_EQ(validated, OSDROP_OK);
        EXPECT_EQ(type, fixture.detail);

        std::size_t length = 0;
        ASSERT_EQ(osdrop_protocol_reencode(kind_of(fixture), bytes.data(), bytes.size(), out.data(),
                                           out.size(), &length),
                  OSDROP_OK);
        EXPECT_EQ(std::vector<std::uint8_t>(out.begin(),
                                            out.begin() + static_cast<std::ptrdiff_t>(length)),
                  bytes);
    }
}

TEST(CapiProtocol, RejectsInvalidArguments) {
    std::uint8_t type = 0;
    std::size_t length = 0;
    const std::array<std::uint8_t, 4> heartbeat{'O', 'D', 1, 0x08};
    EXPECT_EQ(osdrop_protocol_validate(OSDROP_WIRE_CONTROL_MESSAGE, nullptr, 4, &type),
              OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_protocol_validate(OSDROP_WIRE_CONTROL_MESSAGE, heartbeat.data(),
                                       heartbeat.size(), nullptr),
              OSDROP_ERROR_INVALID_ARGUMENT);
    for (const int kind : {0, 4}) {
        EXPECT_EQ(osdrop_protocol_validate(static_cast<osdrop_wire_kind>(kind), heartbeat.data(),
                                           heartbeat.size(), &type),
                  OSDROP_ERROR_INVALID_ARGUMENT);
    }
    EXPECT_EQ(osdrop_protocol_validate(OSDROP_WIRE_TRANSFER_MESSAGE, nullptr, 0, &type),
              OSDROP_ERROR_TRUNCATED);
    EXPECT_EQ(osdrop_protocol_reencode(OSDROP_WIRE_CONTROL_MESSAGE, heartbeat.data(),
                                       heartbeat.size(), nullptr, 0, &length),
              OSDROP_ERROR_BUFFER_TOO_SMALL);
    std::array<std::uint8_t, 3> small{};
    EXPECT_EQ(osdrop_protocol_reencode(OSDROP_WIRE_CONTROL_MESSAGE, heartbeat.data(),
                                       heartbeat.size(), small.data(), small.size(), &length),
              OSDROP_ERROR_BUFFER_TOO_SMALL);
    EXPECT_EQ(osdrop_protocol_reencode(OSDROP_WIRE_CONTROL_MESSAGE, heartbeat.data(),
                                       heartbeat.size(), nullptr, 0, nullptr),
              OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_protocol_reencode(OSDROP_WIRE_CONTROL_MESSAGE, heartbeat.data(),
                                       heartbeat.size(), nullptr, 4, &length),
              OSDROP_ERROR_INVALID_ARGUMENT);
}

} // namespace
