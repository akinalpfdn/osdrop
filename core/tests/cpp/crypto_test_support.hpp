#pragma once

#include "crypto/noise.hpp"

#include <cstdint>
#include <deque>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace osdrop::test {

inline std::vector<std::uint8_t> from_hex(const std::string& hex) {
    if (hex == "-") {
        return {};
    }
    if (hex.size() % 2 != 0) {
        throw std::invalid_argument("odd-length hex");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

inline crypto::Key key_from_hex(const std::string& hex) {
    const auto bytes = from_hex(hex);
    crypto::Key key{};
    if (bytes.size() != key.size()) {
        throw std::invalid_argument("key must be 32 bytes");
    }
    std::copy(bytes.begin(), bytes.end(), key.begin());
    return key;
}

/// Hands out queued byte strings, so tests control "random" ephemeral keys.
class ScriptedRandom final : public crypto::RandomSource {
public:
    void push(std::vector<std::uint8_t> bytes) { queue_.push_back(std::move(bytes)); }

    bool fill(std::span<std::uint8_t> out) override {
        if (queue_.empty() || queue_.front().size() != out.size()) {
            return false;
        }
        std::copy(queue_.front().begin(), queue_.front().end(), out.begin());
        queue_.pop_front();
        return true;
    }

private:
    std::deque<std::vector<std::uint8_t>> queue_;
};

/// Deterministic, distinct bytes per call; good enough for keys in tests.
class CountingRandom final : public crypto::RandomSource {
public:
    bool fill(std::span<std::uint8_t> out) override {
        for (std::uint8_t& byte : out) {
            byte = static_cast<std::uint8_t>(++counter_ * 37U + 11U);
        }
        return true;
    }

private:
    std::uint32_t counter_ = 0;
};

/// Deterministic bytes from a seed; different seeds give different streams.
class SeededRandom final : public crypto::RandomSource {
public:
    explicit SeededRandom(std::uint64_t seed) : state_(seed * 0x9E3779B97F4A7C15ULL + 1) {}

    bool fill(std::span<std::uint8_t> out) override {
        for (std::uint8_t& byte : out) {
            state_ ^= state_ << 13U;
            state_ ^= state_ >> 7U;
            state_ ^= state_ << 17U;
            byte = static_cast<std::uint8_t>(state_);
        }
        return true;
    }

private:
    std::uint64_t state_;
};

inline crypto::KeyPair key_pair(crypto::RandomSource& random) {
    auto pair = crypto::KeyPair::generate(random);
    if (!pair) {
        throw std::runtime_error("key generation failed");
    }
    return pair.value();
}

} // namespace osdrop::test
