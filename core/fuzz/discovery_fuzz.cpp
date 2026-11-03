// Fuzz entry point for the Bonjour TXT parser. The input is split at 0xFF bytes into the six
// values; whatever parses must produce the same values again.
#include "protocol/advertisement.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::array<std::string, 6> fields;
    std::size_t field = 0;
    for (const std::uint8_t byte : std::span<const std::uint8_t>(data, size)) {
        if (byte == 0xFF) {
            if (++field == fields.size()) {
                break;
            }
            continue;
        }
        fields.at(field).push_back(static_cast<char>(byte));
    }
    const osdrop::protocol::TxtValues values{.protocol_version = fields[0],
                                             .protocol_minor = fields[1],
                                             .device_id = fields[2],
                                             .name = fields[3],
                                             .os = fields[4],
                                             .group_id = fields[5]};
    const auto parsed = osdrop::protocol::from_txt(values);
    if (!parsed) {
        return 0;
    }
    const auto written = osdrop::protocol::to_txt(parsed.value());
    if (!written || written.value() != values) {
        std::abort();
    }
    return 0;
}
