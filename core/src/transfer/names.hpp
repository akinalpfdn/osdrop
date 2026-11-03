#pragma once

// Received names as the receiving file system can hold them (DEVPLAN, Phase 13): NFC on every
// platform; on Windows, characters it refuses become their fullwidth look-alikes, trailing dots and
// spaces their look-alikes, and reserved device names get an underscore. Names are cut, not
// refused, when they are longer than the target allows, and names that one folder would hold twice
// on a case-insensitive file system are numbered. A name the target can hold maps to itself.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace osdrop::transfer {

enum class TargetPlatform : std::uint8_t { MacOS, Windows };

/// The platform this build runs on.
[[nodiscard]] TargetPlatform current_platform();

/// One received name (valid per docs/PROTOCOL.md "Conventions") as `target` can create it.
[[nodiscard]] std::string map_name(std::string_view name, TargetPlatform target);

/// Whether `name` fits `target` as it is: what the sending side checks before it offers a name.
[[nodiscard]] bool fits(std::string_view name, TargetPlatform target);

/// A mapped name with " (number)" before its extension (folders: at the end), shortened to fit
/// `target`; `name` itself for number 1.
[[nodiscard]] std::string numbered_name(std::string_view name, int number, bool is_folder,
                                        TargetPlatform target);

/// The paths of one tree, mapped one entry at a time, parents before children.
class TreeNames {
public:
    explicit TreeNames(TargetPlatform target) : target_(target) {}

    /// The mapped relative path of a received entry; nullopt when its parent was not mapped before
    /// (a manifest out of order) or the path is not a valid one.
    [[nodiscard]] std::optional<std::string> map(std::string_view path, bool is_folder);

private:
    TargetPlatform target_;
    std::map<std::string, std::string> folders_;         ///< received → mapped
    std::map<std::string, std::set<std::string>> taken_; ///< mapped folder → folded names in it
};

} // namespace osdrop::transfer
