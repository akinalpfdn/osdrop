#pragma once

#include <cstdint>
#include <span>

// Text rules from docs/PROTOCOL.md, "Conventions".
namespace osdrop::protocol {

/// Valid UTF-8 (no overlong forms, surrogates or values above U+10FFFF) without control
/// characters (U+0000–U+001F, U+007F–U+009F), as every string requires.
[[nodiscard]] bool is_valid_text(std::span<const std::uint8_t> bytes);

/// A Uniform Type Identifier: 1–128 ASCII letters, digits, '.', '-' or '_'.
[[nodiscard]] bool is_valid_type_id(std::span<const std::uint8_t> bytes);

/// One file or folder name: 1–255 bytes of valid text, no '/', not "." or "..".
[[nodiscard]] bool is_valid_name(std::span<const std::uint8_t> bytes);

/// A relative path: 1–1024 bytes of names joined with '/', no empty component.
[[nodiscard]] bool is_valid_path(std::span<const std::uint8_t> bytes);

} // namespace osdrop::protocol
