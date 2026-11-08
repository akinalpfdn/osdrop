#pragma once

// Text as the Windows clipboard holds it, without Windows headers: UTF-16 for CF_UNICODETEXT,
// and the "HTML Format" (CF_HTML), whose header gives the byte offsets of the document and of the
// copied fragment. The shared clipboard carries UTF-8 and plain HTML.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace osdrop::win {

/// UTF-8 of UTF-16 text, up to its first NUL; nullopt for unpaired surrogates.
[[nodiscard]] std::optional<std::string> utf8_of_utf16(std::u16string_view text);
/// UTF-16 of UTF-8 text; nullopt for invalid UTF-8.
[[nodiscard]] std::optional<std::u16string> utf16_of_utf8(std::string_view text);

/// The HTML document of a CF_HTML block (from StartHTML to EndHTML); nullopt when the header is
/// missing or its offsets do not fit the block.
[[nodiscard]] std::optional<std::string> html_of_cf_html(std::span<const std::uint8_t> block);
/// A CF_HTML block with `html` as the copied fragment.
[[nodiscard]] std::vector<std::uint8_t> cf_html_of_html(std::string_view html);

} // namespace osdrop::win
