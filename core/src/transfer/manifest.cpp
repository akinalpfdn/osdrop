#include "transfer/manifest.hpp"

#include "protocol/constants.hpp"
#include "protocol/text.hpp"
#include "protocol/wire.hpp"
#include "transfer/files.hpp"
#include "transfer/unicode.hpp"

#include <algorithm>
#include <ranges>
#include <string>
#include <system_error>
#include <utility>

namespace osdrop::transfer {
namespace {

namespace fs = std::filesystem;
using protocol::EntryKind;
using protocol::ManifestEntry;

constexpr std::size_t kManifestHeaderSize = protocol::kHeaderSize + 8; // id, is_last, count
constexpr std::size_t kEntryFieldsSize = 22; // path length, kind, flags, reserved, size, time

/// Names on the wire are limited to 255 bytes of UTF-8, as on macOS, so numbering duplicates
/// uses that platform's limit.
constexpr TargetPlatform kWireNames = TargetPlatform::MacOS;

std::size_t entry_size(const ManifestEntry& entry) {
    return kEntryFieldsSize + entry.path.size();
}

std::vector<std::string> split(std::string_view path) {
    std::vector<std::string> parts;
    for (const auto part : std::views::split(path, '/')) {
        parts.emplace_back(part.begin(), part.end());
    }
    return parts;
}

std::string join(std::span<const std::string> parts) {
    std::string out;
    for (const std::string& part : parts) {
        out += out.empty() ? part : "/" + part;
    }
    return out;
}

/// A file name as it travels: NFC, or nullopt when the protocol cannot carry it.
std::optional<std::string> wire_name(const fs::path& local) {
    auto name = to_nfc(utf8_of(local.filename()));
    if (!name || !protocol::is_valid_name(protocol::bytes_of(*name))) {
        return std::nullopt;
    }
    return name;
}

/// `name`, numbered when a name in `taken` (folded) equals it; records the result.
std::string unique_in(std::set<std::string>& taken, const std::string& name, bool is_folder) {
    std::string chosen = name;
    for (int number = 2; taken.contains(fold_case(chosen).value_or(chosen)); ++number) {
        chosen = numbered_name(name, number, is_folder, kWireNames);
    }
    taken.insert(fold_case(chosen).value_or(chosen));
    return chosen;
}

class Scanner {
public:
    explicit Scanner(std::size_t max_entries) : max_entries_(max_entries) {}

    Status<Error> add_root(const fs::path& root) {
        const fs::path local = root.has_filename() ? root : root.parent_path();
        std::error_code error;
        const fs::file_status status = fs::status(native_path(local), error); // follows links
        if (error || !fs::exists(status)) {
            return Status<Error>::failure(Error::NotFound);
        }
        const auto name = wire_name(local);
        const bool folder = fs::is_directory(status);
        if (!name || !(folder || fs::is_regular_file(status))) {
            ++tree_.skipped;
            return Status<Error>::ok();
        }
        const std::string path = unique_in(roots_, *name, folder);
        if (folder) {
            return add_folder(local, path);
        }
        return add_file(local, path);
    }

    /// Lists the folders found so far, parents before children.
    Status<Error> walk() {
        while (!pending_.empty()) {
            auto [local, path] = std::move(pending_.back());
            pending_.pop_back();
            if (const auto listed = list(local, path); !listed) {
                return listed;
            }
        }
        return Status<Error>::ok();
    }

    SourceTree take() { return std::move(tree_); }

private:
    Status<Error> list(const fs::path& local, const std::string& path) {
        std::error_code error;
        std::vector<fs::directory_entry> children;
        for (fs::directory_iterator it(native_path(local), error), end; !error && it != end;
             it.increment(error)) {
            children.push_back(*it);
        }
        if (error) {
            ++tree_.skipped; // unreadable: the folder arrives empty
            return Status<Error>::ok();
        }
        std::ranges::sort(children, {}, [](const fs::directory_entry& e) { return e.path(); });
        std::set<std::string> taken;
        for (const fs::directory_entry& child : children) {
            if (const auto added = add_child(child, path, taken); !added) {
                return added;
            }
        }
        return Status<Error>::ok();
    }

    Status<Error> add_child(const fs::directory_entry& child, const std::string& parent,
                            std::set<std::string>& taken) {
        std::error_code error;
        const fs::file_status status = child.symlink_status(error);
        const auto name = wire_name(child.path());
        const bool folder = fs::is_directory(status);
        if (error || !name) {
            ++tree_.skipped;
            return Status<Error>::ok();
        }
        const std::string path = parent + "/" + unique_in(taken, *name, folder);
        if (!protocol::is_valid_path(protocol::bytes_of(path))) {
            ++tree_.skipped;
            return Status<Error>::ok();
        }
        if (fs::is_symlink(status)) {
            return add_link(child.path(), path);
        }
        if (folder) {
            return add_folder(child.path(), path);
        }
        if (fs::is_regular_file(status)) {
            return add_file(child.path(), path);
        }
        ++tree_.skipped; // devices, sockets, pipes
        return Status<Error>::ok();
    }

    Status<Error> add_folder(const fs::path& local, const std::string& path) {
        pending_.emplace_back(local, path);
        return push({.path = path,
                     .kind = EntryKind::Folder,
                     .executable = false,
                     .byte_count = 0,
                     .modified_us = modified_us(local).value_or(0)},
                    {.local = local, .link_target = {}});
    }

    Status<Error> add_file(const fs::path& local, const std::string& path) {
        std::error_code error;
        const std::uintmax_t size = fs::file_size(native_path(local), error);
        if (error) {
            ++tree_.skipped;
            return Status<Error>::ok();
        }
        tree_.total_bytes += size;
        return push({.path = path,
                     .kind = EntryKind::File,
                     .executable = is_executable(local),
                     .byte_count = size,
                     .modified_us = modified_us(local).value_or(0)},
                    {.local = local, .link_target = {}});
    }

    Status<Error> add_link(const fs::path& local, const std::string& path) {
        std::error_code error;
        const fs::path target = fs::read_symlink(native_path(local), error);
        std::string text = to_nfc(utf8_of(target.generic_u8string())).value_or(std::string{});
        if (error || text.empty() || text.size() > protocol::kMaxLinkTarget) {
            ++tree_.skipped;
            return Status<Error>::ok();
        }
        const std::size_t size = text.size();
        return push({.path = path,
                     .kind = EntryKind::Link,
                     .executable = false,
                     .byte_count = size,
                     .modified_us = link_modified_us(local).value_or(0)},
                    {.local = local, .link_target = std::move(text)});
    }

    Status<Error> push(ManifestEntry entry, SourceDetail source) {
        if (tree_.entries.size() >= max_entries_) {
            return Status<Error>::failure(Error::TooLarge);
        }
        tree_.entries.push_back(std::move(entry));
        tree_.sources.push_back(std::move(source));
        return Status<Error>::ok();
    }

    std::size_t max_entries_;
    SourceTree tree_;
    std::set<std::string> roots_;
    std::vector<std::pair<fs::path, std::string>> pending_;
};

} // namespace

Expected<SourceTree, Error> scan(std::span<const fs::path> roots, std::size_t max_entries) {
    Scanner scanner(max_entries);
    for (const fs::path& root : roots) {
        if (const auto added = scanner.add_root(root); !added) {
            return Expected<SourceTree, Error>::failure(added.error());
        }
        if (const auto walked = scanner.walk(); !walked) {
            return Expected<SourceTree, Error>::failure(walked.error());
        }
    }
    return scanner.take();
}

std::size_t manifest_size(std::span<const ManifestEntry> entries) {
    std::size_t size = kManifestHeaderSize;
    for (const ManifestEntry& entry : entries) {
        size += entry_size(entry);
    }
    return size;
}

std::vector<protocol::Manifest> manifest_messages(std::uint32_t request_id,
                                                  std::span<const ManifestEntry> entries) {
    std::vector<protocol::Manifest> parts;
    std::size_t size = kManifestHeaderSize;
    for (const ManifestEntry& entry : entries) {
        if (parts.empty() || parts.back().entries.size() == protocol::kMaxManifestEntries ||
            size + entry_size(entry) > protocol::kMaxTransferMessageSize) {
            parts.push_back({.request_id = request_id, .is_last = false, .entries = {}});
            size = kManifestHeaderSize;
        }
        parts.back().entries.push_back(entry);
        size += entry_size(entry);
    }
    if (!parts.empty()) {
        parts.back().is_last = true;
    }
    return parts;
}

Status<Error> ManifestCollector::add(const protocol::Manifest& part) {
    if (complete_) {
        return Status<Error>::failure(Error::InvalidState);
    }
    for (const ManifestEntry& entry : part.entries) {
        if (plan_.entries.size() >= max_entries_) {
            return Status<Error>::failure(Error::TooLarge);
        }
        if (plan_.index_of.contains(entry.path)) {
            return Status<Error>::failure(Error::InvalidArgument);
        }
        auto mapped = names_.map(entry.path, entry.kind == EntryKind::Folder);
        if (!mapped) {
            return Status<Error>::failure(Error::InvalidArgument);
        }
        if (entry.kind == EntryKind::File) {
            if (entry.byte_count > UINT64_MAX - plan_.total_bytes) {
                return Status<Error>::failure(Error::TooLarge);
            }
            plan_.total_bytes += entry.byte_count;
            ++plan_.file_count;
        }
        plan_.index_of.emplace(entry.path, plan_.entries.size());
        plan_.entries.push_back({.entry = entry, .target = std::move(*mapped)});
    }
    complete_ = part.is_last;
    return Status<Error>::ok();
}

namespace {

/// The entry `target` leads to from `link`, inside the link's own item; nullopt for none.
std::optional<std::size_t> resolved_entry(const ReceivePlan& plan, std::size_t link,
                                          std::string_view target) {
    const PlannedEntry& entry = plan.entries.at(link);
    std::vector<std::string> resolved = split(entry.entry.path);
    resolved.pop_back();
    for (const std::string& part : split(target)) {
        if (part.empty() || part == ".") {
            continue;
        }
        if (part != "..") {
            resolved.push_back(part);
        } else if (resolved.empty()) {
            return std::nullopt;
        } else {
            resolved.pop_back();
        }
    }
    // Inside the link's own item: other items may get another top-level name on disk.
    const std::vector<std::string> link_parts = split(entry.entry.path);
    if (resolved.empty() || resolved.front() != link_parts.front()) {
        return std::nullopt;
    }
    const auto found = plan.index_of.find(join(resolved));
    if (found == plan.index_of.end() || found->second == link) {
        return std::nullopt;
    }
    return found->second;
}

} // namespace

std::optional<std::string> mapped_link_target(const ReceivePlan& plan, std::size_t link,
                                              std::string_view target) {
    const bool drive =
        target.size() >= 2 && target[1] == ':' &&
        ((target[0] >= 'A' && target[0] <= 'Z') || (target[0] >= 'a' && target[0] <= 'z'));
    if (target.empty() || target.front() == '/' || target.front() == '\\' || drive) {
        return std::nullopt;
    }
    // A backslash is part of a name on a Mac and a separator on Windows, where no name has one:
    // the target is read as it is first, and with backslashes as separators only when that
    // leads nowhere.
    std::optional<std::size_t> found = resolved_entry(plan, link, target);
    if (!found && target.find('\\') != std::string_view::npos) {
        std::string separated(target);
        std::ranges::replace(separated, '\\', '/');
        found = resolved_entry(plan, link, separated);
    }
    if (!found) {
        return std::nullopt;
    }
    std::vector<std::string> from = split(plan.entries.at(link).target);
    from.pop_back();
    const std::vector<std::string> to = split(plan.entries.at(*found).target);
    const auto [from_end, to_end] = std::ranges::mismatch(from, to);
    std::vector<std::string> relative(static_cast<std::size_t>(from.end() - from_end), "..");
    relative.insert(relative.end(), to_end, to.end());
    return relative.empty() ? std::string(".") : join(relative);
}

} // namespace osdrop::transfer
