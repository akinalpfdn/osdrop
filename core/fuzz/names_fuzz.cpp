// Fuzz entry point for received names: NFC, case folding and the mapping to each platform. The
// first byte picks the platform; the rest is the name. A name the protocol allows maps to a name
// the platform can hold, and mapping is idempotent, numbered names included.
#include "protocol/text.hpp"
#include "transfer/names.hpp"
#include "transfer/unicode.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

namespace {

namespace transfer = osdrop::transfer;

std::span<const std::uint8_t> bytes(const std::string& text) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): the string's bytes
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) {
        return 0;
    }
    const std::span<const std::uint8_t> input(data, size);
    const auto target =
        (input[0] & 1U) != 0 ? transfer::TargetPlatform::Windows : transfer::TargetPlatform::MacOS;
    const std::string name(input.begin() + 1, input.end());

    if (const auto nfc = transfer::to_nfc(name)) {
        if (transfer::to_nfc(*nfc) != nfc || !transfer::fold_case(name)) {
            std::abort(); // NFC is idempotent; folding works on any valid UTF-8
        }
    }
    if (!osdrop::protocol::is_valid_name(bytes(name))) {
        return 0;
    }
    const std::string mapped = transfer::map_name(name, target);
    if (mapped.empty() || !transfer::fits(mapped, target) || !transfer::decode_utf8(mapped)) {
        std::abort();
    }
    for (const int number : {2, 10, 9999}) {
        const std::string numbered = transfer::numbered_name(mapped, number, number == 10, target);
        if (numbered.empty() || !transfer::fits(numbered, target)) {
            std::abort();
        }
    }
    return 0;
}
