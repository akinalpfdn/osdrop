// Fuzz entry point for the decoder of transfer messages, manifests included.
#include "fuzz_check.hpp"
#include "protocol/transfer_messages.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    osdrop::fuzz::check_round_trip(std::span<const std::uint8_t>(data, size),
                                   osdrop::protocol::decode_transfer,
                                   osdrop::protocol::encode_transfer);
    return 0;
}
