#include "capi/random_source.hpp"
#include "crypto/noise.hpp"
#include "protocol/constants.hpp"

#include <osdrop/osdrop.h>

#include <algorithm>
#include <span>

namespace {

static_assert(OSDROP_KEY_SIZE == osdrop::crypto::kKeySize);
static_assert(OSDROP_DEVICE_ID_SIZE == osdrop::protocol::kIdSize);

osdrop_result to_result(osdrop::Error error) {
    return static_cast<osdrop_result>(error);
}

osdrop::crypto::Key key_from(const std::uint8_t* bytes) {
    osdrop::crypto::Key key{};
    std::copy_n(bytes, key.size(), key.begin());
    return key;
}

void write_key(const osdrop::crypto::Key& key, std::uint8_t* out) {
    std::ranges::copy(key, out);
}

} // namespace

extern "C" {

osdrop_result osdrop_identity_generate(const osdrop_random_source* random, uint8_t* secret_key,
                                       uint8_t* public_key) {
    if (random == nullptr || random->fill == nullptr || secret_key == nullptr ||
        public_key == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    osdrop::capi::CallbackRandomSource source(*random);
    const auto pair = osdrop::crypto::KeyPair::generate(source);
    if (!pair) {
        return to_result(pair.error());
    }
    write_key(pair.value().secret(), secret_key);
    write_key(pair.value().public_key(), public_key);
    return OSDROP_OK;
}

osdrop_result osdrop_identity_public_key(const uint8_t* secret_key, uint8_t* public_key) {
    if (secret_key == nullptr || public_key == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    osdrop::crypto::Key secret = key_from(secret_key);
    const auto pair = osdrop::crypto::KeyPair::from_secret(secret);
    osdrop::crypto::wipe(secret);
    if (!pair) {
        return to_result(pair.error());
    }
    write_key(pair.value().public_key(), public_key);
    return OSDROP_OK;
}

osdrop_result osdrop_identity_device_id(const uint8_t* public_key, uint8_t* device_id) {
    if (public_key == nullptr || device_id == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const auto id = osdrop::crypto::device_id(key_from(public_key));
    std::ranges::copy(id, device_id);
    return OSDROP_OK;
}

} // extern "C"
