#pragma once

#include "crypto/primitives.hpp"

#include <osdrop/osdrop.h>

namespace osdrop::capi {

/// Adapts the platform's C callback to crypto::RandomSource.
class CallbackRandomSource final : public crypto::RandomSource {
public:
    explicit CallbackRandomSource(const osdrop_random_source& source) : source_(source) {}

    bool fill(std::span<std::uint8_t> out) override {
        return source_.fill != nullptr &&
               source_.fill(source_.context, out.data(), out.size()) == OSDROP_OK;
    }

private:
    osdrop_random_source source_;
};

} // namespace osdrop::capi
