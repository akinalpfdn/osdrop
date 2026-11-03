// Fuzz entry point for the decoder of control messages.
#include "fuzz_check.hpp"
#include "protocol/control_messages.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    osdrop::fuzz::check_round_trip(std::span<const std::uint8_t>(data, size),
                                   osdrop::protocol::decode_control,
                                   osdrop::protocol::encode_control);
    return 0;
}
