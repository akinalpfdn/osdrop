#include "transfer/names.hpp"

#include "protocol/text.hpp"
#include "protocol/wire.hpp"
#include "transfer/unicode.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <string>
#include <utility>

namespace osdrop::transfer {
namespace {

constexpr std::size_t kMacNameBytes = 255;     // APFS and HFS+: bytes of UTF-8
constexpr std::size_t kWindowsNameUnits = 255; // NTFS: UTF-16 code units
constexpr std::size_t kMaxExtension = 16;      // longer "extensions" are part of the name

/// Characters Windows refuses in a name, and the fullwidth forms they become.
constexpr std::array<std::pair<char32_t, char32_t>, 9> kWindowsInvalid{{{U'<', U'＜'},
                                                                        {U'>', U'＞'},
                                                                        {U':', U'：'},
                                                                        {U'"', U'＂'},
                                                                        {U'\\', U'＼'},
                                                                        {U'|', U'｜'},
                                                                        {U'?', U'？'},
                                                                        {U'*', U'＊'},
                                                                        {U'/', U'／'}}};
constexpr char32_t kFullwidthStop = U'．';
constexpr char32_t kNoBreakSpace = U' ';

/// Device names Windows reserves in any folder, with or without an extension.
constexpr std::array<std::u32string_view, 4> kReservedNames{U"CON", U"PRN", U"AUX", U"NUL"};
constexpr std::array<std::u32string_view, 2> kReservedPorts{U"COM", U"LPT"};

char32_t ascii_upper(char32_t cp) {
    return cp >= U'a' && cp <= U'z' ? cp - (U'a' - U'A') : cp;
}

bool equals_ascii_upper(std::u32string_view text, std::u32string_view upper) {
    return text.size() == upper.size() &&
           std::ranges::equal(text, upper,
                              [](char32_t a, char32_t b) { return ascii_upper(a) == b; });
}

bool reserved(std::u32string_view base) {
    if (std::ranges::any_of(kReservedNames,
                            [&](auto name) { return equals_ascii_upper(base, name); })) {
        return true;
    }
    if (base.size() != 4) {
        return false;
    }
    const char32_t digit = base[3];
    const bool port_digit =
        (digit >= U'0' && digit <= U'9') || digit == U'¹' || digit == U'²' || digit == U'³';
    return port_digit && std::ranges::any_of(kReservedPorts, [&](auto port) {
               return equals_ascii_upper(base.substr(0, 3), port);
           });
}

void make_windows_safe(std::u32string& name) {
    for (char32_t& cp : name) {
        if (const auto* it =
                std::ranges::find(kWindowsInvalid, cp, &std::pair<char32_t, char32_t>::first);
            it != kWindowsInvalid.end()) {
            cp = it->second;
        }
    }
    // Windows drops trailing dots and spaces; their look-alikes keep the name as it was.
    for (auto it = name.rbegin(); it != name.rend() && (*it == U'.' || *it == U' '); ++it) {
        *it = *it == U'.' ? kFullwidthStop : kNoBreakSpace;
    }
    const std::size_t dot = name.find(U'.');
    const std::u32string_view base = std::u32string_view(name).substr(0, dot);
    if (reserved(base)) {
        name.insert(base.size(), 1, U'_');
    }
}

std::size_t length_on(std::u32string_view name, TargetPlatform target) {
    if (target == TargetPlatform::MacOS) {
        return encode_utf8(name).size();
    }
    return std::accumulate(
        name.begin(), name.end(), std::size_t{0},
        [](std::size_t units, char32_t cp) { return units + (cp > 0xFFFF ? 2 : 1); });
}

std::size_t limit_of(TargetPlatform target) {
    return target == TargetPlatform::MacOS ? kMacNameBytes : kWindowsNameUnits;
}

/// The extension (from the last dot) when the name has one worth keeping.
std::size_t extension_start(std::u32string_view name) {
    const std::size_t dot = name.rfind(U'.');
    if (dot == std::u32string_view::npos || dot == 0 || name.size() - dot > kMaxExtension) {
        return name.size();
    }
    return dot;
}

/// Shortens the part before the extension until `base + suffix + extension` fits the target.
std::u32string fit(std::u32string_view name, std::u32string_view suffix, TargetPlatform target) {
    const std::size_t split = extension_start(name);
    std::u32string base(name.substr(0, split));
    const std::u32string_view extension = name.substr(split);
    const auto whole = [&] { return base + std::u32string(suffix) + std::u32string(extension); };
    while (!base.empty() && length_on(whole(), target) > limit_of(target)) {
        base.pop_back();
    }
    return whole();
}

std::u32string mapped_code_points(std::string_view name, TargetPlatform target) {
    const std::string normalized = to_nfc(name).value_or(std::string(name));
    std::u32string code_points = decode_utf8(normalized).value_or(std::u32string{});
    if (target == TargetPlatform::Windows) {
        make_windows_safe(code_points);
    }
    if (length_on(code_points, target) > limit_of(target)) {
        code_points = fit(code_points, {}, target);
    }
    return code_points;
}

} // namespace

TargetPlatform current_platform() {
#ifdef _WIN32
    return TargetPlatform::Windows;
#else
    return TargetPlatform::MacOS;
#endif
}

std::string map_name(std::string_view name, TargetPlatform target) {
    return encode_utf8(mapped_code_points(name, target));
}

bool fits(std::string_view name, TargetPlatform target) {
    return map_name(name, target) == name;
}

std::optional<std::string> TreeNames::map(std::string_view path, bool is_folder) {
    if (!protocol::is_valid_path(protocol::bytes_of(std::string(path)))) {
        return std::nullopt;
    }
    const std::size_t slash = path.rfind('/');
    std::string mapped_parent;
    if (slash != std::string_view::npos) {
        const auto parent = folders_.find(std::string(path.substr(0, slash)));
        if (parent == folders_.end()) {
            return std::nullopt;
        }
        mapped_parent = parent->second;
    }
    const std::string_view leaf = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::u32string mapped = mapped_code_points(leaf, target_);
    std::set<std::string>& taken = taken_[mapped_parent];
    std::u32string chosen = mapped;
    // Folders have no extension: "Photos.2024" numbers as "Photos.2024 (2)".
    for (int number = 2;
         taken.contains(fold_case(encode_utf8(chosen)).value_or(encode_utf8(chosen))); ++number) {
        const std::u32string suffix =
            U" (" + decode_utf8(std::to_string(number)).value_or(U"") + U")";
        chosen = is_folder ? fit(mapped + suffix, {}, target_) : fit(mapped, suffix, target_);
    }
    const std::string name = encode_utf8(chosen);
    taken.insert(fold_case(name).value_or(name));
    std::string full = mapped_parent.empty() ? name : mapped_parent + "/" + name;
    if (is_folder) {
        folders_[std::string(path)] = full;
    }
    return full;
}

} // namespace osdrop::transfer
