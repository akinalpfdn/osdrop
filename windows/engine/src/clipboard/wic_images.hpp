#pragma once

// Clipboard images between Windows' device-independent bitmaps (CF_DIB, CF_DIBV5) and the PNG the
// shared clipboard carries, through the Windows Imaging Component. Call on a thread where COM is
// initialized.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace osdrop::engine {

/// A PNG of a packed DIB (a BITMAPINFOHEADER or BITMAPV5HEADER, its color table or masks, then
/// the pixels).
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
png_of_dib(std::span<const std::uint8_t> dib);

/// A packed 32-bit bottom-up DIB of a PNG, for applications that paste only CF_DIB.
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
dib_of_png(std::span<const std::uint8_t> png);

} // namespace osdrop::engine
