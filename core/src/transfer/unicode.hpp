#pragma once

// Unicode for file names (docs/PROTOCOL.md "Conventions": names are sent in NFC and normalized
// again by receivers): UTF-8 decoding, Normalization Form C, and the simple lowercase folding that
// tells which names a case-insensitive file system treats as one. Tables are generated from the
// Unicode Character Database (core/tools/generate_unicode_tables.py).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace osdrop::transfer {

/// Code points of valid UTF-8; nullopt for invalid UTF-8 (overlong, surrogates, above U+10FFFF).
[[nodiscard]] std::optional<std::u32string> decode_utf8(std::string_view text);
[[nodiscard]] std::string encode_utf8(std::u32string_view code_points);

/// Normalization Form C of valid UTF-8; nullopt for invalid UTF-8.
[[nodiscard]] std::optional<std::string> to_nfc(std::string_view text);

/// NFC with each code point replaced by its simple lowercase; nullopt for invalid UTF-8.
[[nodiscard]] std::optional<std::string> fold_case(std::string_view text);

/// The Unicode version of the tables.
[[nodiscard]] std::string_view unicode_version();

} // namespace osdrop::transfer
