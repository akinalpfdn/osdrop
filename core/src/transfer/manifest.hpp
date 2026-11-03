#pragma once

// Manifests (docs/PROTOCOL.md, "0x83 Manifest"). The sending side scans the items it offers into
// entries, parents before children, without following links inside them, with names in NFC; what
// cannot travel (names the protocol refuses, devices and sockets, link targets over 4096 bytes) is
// left out and counted. The receiving side checks the entries as they arrive and maps each path
// to the names this computer can create (names.hpp).

#include "protocol/transfer_messages.hpp"
#include "transfer/names.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace osdrop::transfer {

/// Entries one item may have, on either side.
inline constexpr std::size_t kMaxTreeEntries = 1'000'000;

/// Where an entry of a scanned tree comes from.
struct SourceDetail {
    std::filesystem::path local;
    std::string link_target; ///< links: the target, '/' separated
};

struct SourceTree {
    std::vector<protocol::ManifestEntry> entries;
    std::vector<SourceDetail> sources; ///< one per entry
    std::uint64_t total_bytes = 0;     ///< of the files
    std::size_t skipped = 0;
};

/// The trees at `roots` (absolute paths). The roots themselves are followed when they are links;
/// two roots with the same name are numbered. NOT_FOUND when a root does not exist, TOO_LARGE
/// beyond `max_entries`.
[[nodiscard]] Expected<SourceTree, Error> scan(std::span<const std::filesystem::path> roots,
                                               std::size_t max_entries = kMaxTreeEntries);

/// `entries` as Manifest messages that each fit in one transfer message; at least one message,
/// the last one with is_last set. `entries` must not be empty.
[[nodiscard]] std::vector<protocol::Manifest>
manifest_messages(std::uint32_t request_id, std::span<const protocol::ManifestEntry> entries);

/// The encoded size of a Manifest message with these entries.
[[nodiscard]] std::size_t manifest_size(std::span<const protocol::ManifestEntry> entries);

struct PlannedEntry {
    protocol::ManifestEntry entry; ///< as received
    std::string target;            ///< mapped path, '/' separated, relative to the destination
};

struct ReceivePlan {
    std::vector<PlannedEntry> entries;
    std::map<std::string, std::size_t> index_of; ///< received path → entry
    std::uint64_t total_bytes = 0;               ///< of the files
    std::size_t file_count = 0;
};

/// Collects a manifest part by part: parents before children, each path once, folders and links
/// as the protocol describes them.
class ManifestCollector {
public:
    explicit ManifestCollector(TargetPlatform target, std::size_t max_entries = kMaxTreeEntries)
        : names_(target), max_entries_(max_entries) {}

    /// INVALID_ARGUMENT for an entry out of order or seen before, TOO_LARGE beyond max_entries,
    /// INVALID_STATE after the last part.
    [[nodiscard]] Status<Error> add(const protocol::Manifest& part);
    [[nodiscard]] bool complete() const { return complete_; }
    /// The plan so far; whole once complete().
    [[nodiscard]] const ReceivePlan& plan() const { return plan_; }
    [[nodiscard]] ReceivePlan take() { return std::move(plan_); }

private:
    TreeNames names_;
    std::size_t max_entries_;
    ReceivePlan plan_;
    bool complete_ = false;
};

/// The target to give a received link so that it names the same entry after mapping: a relative
/// path from the link's folder, '/' separated. nullopt when `target` is absolute, leaves the
/// link's item or names no entry of it: such links are not recreated.
[[nodiscard]] std::optional<std::string>
mapped_link_target(const ReceivePlan& plan, std::size_t link, std::string_view target);

} // namespace osdrop::transfer
