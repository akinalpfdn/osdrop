#include "crypto_test_support.hpp"

#include <osdrop/osdrop.h>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace {

using osdrop::test::from_hex;

const std::vector<std::uint8_t> kAliceSecret =
    from_hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
const std::vector<std::uint8_t> kAlicePublic =
    from_hex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");

osdrop_result alice_random(void* /*context*/, uint8_t* buffer, size_t length) {
    if (length != kAliceSecret.size()) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    std::copy(kAliceSecret.begin(), kAliceSecret.end(), buffer);
    return OSDROP_OK;
}

osdrop_result broken_random(void* /*context*/, uint8_t* /*buffer*/, size_t /*length*/) {
    return OSDROP_ERROR_RANDOM_UNAVAILABLE;
}

TEST(CapiIdentity, GeneratesFromThePlatformRandomSource) {
    const osdrop_random_source random{.fill = alice_random, .context = nullptr};
    std::array<std::uint8_t, OSDROP_KEY_SIZE> secret{};
    std::array<std::uint8_t, OSDROP_KEY_SIZE> public_key{};
    ASSERT_EQ(osdrop_identity_generate(&random, secret.data(), public_key.data()), OSDROP_OK);
    EXPECT_EQ(std::vector<std::uint8_t>(secret.begin(), secret.end()), kAliceSecret);
    EXPECT_EQ(std::vector<std::uint8_t>(public_key.begin(), public_key.end()), kAlicePublic);
}

TEST(CapiIdentity, ReportsRandomFailureAndInvalidArguments) {
    const osdrop_random_source broken{.fill = broken_random, .context = nullptr};
    std::array<std::uint8_t, OSDROP_KEY_SIZE> secret{};
    std::array<std::uint8_t, OSDROP_KEY_SIZE> public_key{};
    EXPECT_EQ(osdrop_identity_generate(&broken, secret.data(), public_key.data()),
              OSDROP_ERROR_RANDOM_UNAVAILABLE);
    const osdrop_random_source missing{.fill = nullptr, .context = nullptr};
    EXPECT_EQ(osdrop_identity_generate(&missing, secret.data(), public_key.data()),
              OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_identity_generate(nullptr, secret.data(), public_key.data()),
              OSDROP_ERROR_INVALID_ARGUMENT);
}

TEST(CapiIdentity, ImportsAStoredSecretAndRejectsAZeroOne) {
    std::array<std::uint8_t, OSDROP_KEY_SIZE> public_key{};
    ASSERT_EQ(osdrop_identity_public_key(kAliceSecret.data(), public_key.data()), OSDROP_OK);
    EXPECT_EQ(std::vector<std::uint8_t>(public_key.begin(), public_key.end()), kAlicePublic);

    const std::array<std::uint8_t, OSDROP_KEY_SIZE> zero{};
    EXPECT_EQ(osdrop_identity_public_key(zero.data(), public_key.data()), OSDROP_ERROR_INVALID_KEY);
}

TEST(CapiIdentity, DerivesTheDeviceId) {
    std::array<std::uint8_t, OSDROP_DEVICE_ID_SIZE> id{};
    ASSERT_EQ(osdrop_identity_device_id(kAlicePublic.data(), id.data()), OSDROP_OK);
    EXPECT_EQ(std::vector<std::uint8_t>(id.begin(), id.end()),
              from_hex("7a91869e4c114964206972403b0a3d81"));
    EXPECT_EQ(osdrop_identity_device_id(nullptr, id.data()), OSDROP_ERROR_INVALID_ARGUMENT);
}

} // namespace
