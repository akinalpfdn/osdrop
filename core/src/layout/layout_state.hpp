#pragma once

// What a computer keeps about the arrangement between runs (docs/ARCHITECTURE.md, "Layout
// state"): the group it belongs to, the layout document and the screens each member last
// reported, so the arrangement editor can show members that are offline. Platforms store the
// encoded bytes with their settings (UserDefaults, JSON); core never writes files.

#include "layout/document.hpp"
#include "layout/screens.hpp"
#include "protocol/control_messages.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace osdrop::layout {

inline constexpr std::uint8_t kLayoutStateVersion = 1;

struct LayoutState {
    protocol::Id group_id{}; ///< the group the document belongs to; zeros in no group
    LayoutDocument document;
    ScreenMap screens; ///< members' last reports, this computer's included
    bool operator==(const LayoutState&) const = default;
};

[[nodiscard]] std::vector<std::uint8_t> encode_layout(const LayoutState& state);
/// Fails with UNSUPPORTED_VERSION for a state written by a newer Osdrop.
[[nodiscard]] Expected<LayoutState, Error> decode_layout(std::span<const std::uint8_t> bytes);

} // namespace osdrop::layout
