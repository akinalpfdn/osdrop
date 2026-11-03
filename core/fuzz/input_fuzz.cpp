// Fuzz entry point for the decoder of input datagrams.
#include "fuzz_check.hpp"
#include "protocol/input_messages.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    osdrop::fuzz::check_round_trip(std::span<const std::uint8_t>(data, size),
                                   osdrop::protocol::decode_input, osdrop::protocol::encode_input);
    return 0;
}
