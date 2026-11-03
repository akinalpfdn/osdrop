// Fuzz entry point for the arrangement: the stored layout state, and ScreenReport and LayoutState
// from members. Properties checked on any input (run under ASan/UBSan): nothing crashes or reads
// out of bounds; a decoded state encodes canonically and decodes back to itself; the cursor never
// leaves a reported desktop, whatever it is moved by; and layout documents merge the same way in
// either order.
#include "layout/document.hpp"
#include "layout/layout_state.hpp"
#include "layout/screens.hpp"
#include "protocol/control_messages.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <variant>
#include <vector>

namespace {

namespace layout = osdrop::layout;
namespace protocol = osdrop::protocol;

void check(bool condition) {
    if (!condition) {
        std::abort();
    }
}

void fuzz_state(std::span<const std::uint8_t> input) {
    const auto decoded = layout::decode_layout(input);
    if (!decoded) {
        return;
    }
    const std::vector<std::uint8_t> encoded = layout::encode_layout(decoded.value());
    const auto again = layout::decode_layout(encoded);
    check(again.has_value() && again.value() == decoded.value());
    check(layout::encode_layout(again.value()) == encoded);
}

/// Moves the cursor around a reported desktop by deltas made from the input bytes.
void fuzz_screens(const protocol::ScreenReport& report, std::span<const std::uint8_t> input) {
    const layout::Screens screens = layout::Screens::from_report(report);
    static_cast<void>(layout::derive_frames(screens.monitors()));
    layout::Point cursor = screens.clamp({});
    for (std::size_t i = 0; i + 1 < input.size(); i += 2) {
        const layout::Point delta{.x = static_cast<std::int8_t>(input[i]) * 4096LL,
                                  .y = static_cast<std::int8_t>(input[i + 1]) * 4096LL};
        const layout::Motion moved = screens.move(cursor, delta);
        check(screens.monitor_at(moved.position).has_value());
        cursor = moved.position;
    }
}

void fuzz_document(const protocol::LayoutState& message) {
    const layout::LayoutDocument received = layout::LayoutDocument::from_message(message);
    const layout::LayoutDocument known{
        .revision = 5, .author = {9}, .placements = {{layout::DeviceId{9}, {}}}};
    layout::LayoutDocument known_then_received = known;
    known_then_received.merge(received);
    layout::LayoutDocument received_then_known = received;
    received_then_known.merge(known);
    check(known_then_received == received_then_known);
    check(!known_then_received.merge(received));
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);
    fuzz_state(input);
    const auto message = protocol::decode_control(input);
    if (!message) {
        return 0;
    }
    if (const auto* report = std::get_if<protocol::ScreenReport>(&message.value())) {
        fuzz_screens(*report, input);
    } else if (const auto* document = std::get_if<protocol::LayoutState>(&message.value())) {
        fuzz_document(*document);
    }
    return 0;
}
