#pragma once

// Shared property of the decoder fuzz targets: whatever decodes re-encodes to exactly the same
// bytes (encoding is canonical). Run under ASan/UBSan, the targets also check that no input
// crashes or reads out of bounds.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace osdrop::fuzz {

template <typename Decode, typename Encode>
void check_round_trip(std::span<const std::uint8_t> input, Decode decode, Encode encode) {
    const auto decoded = decode(input);
    if (!decoded) {
        return;
    }
    const auto encoded = encode(decoded.value());
    if (!encoded || !std::ranges::equal(encoded.value(), input)) {
        std::abort();
    }
}

} // namespace osdrop::fuzz
