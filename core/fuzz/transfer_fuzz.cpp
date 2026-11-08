// Fuzz entry point for the decoder of transfer messages, manifests included. A decoded manifest
// also goes through the receiver's checks and name mapping for both platforms, and every link in
// it is pointed at every entry: a link target that resolves must lead to that entry's mapped path.
#include "fuzz_check.hpp"
#include "protocol/transfer_messages.hpp"
#include "transfer/manifest.hpp"
#include "transfer/names.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace {

namespace protocol = osdrop::protocol;
namespace transfer = osdrop::transfer;

std::vector<std::string> split(const std::string& path) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t slash = path.find('/'); slash != std::string::npos;
         slash = path.find('/', start)) {
        parts.push_back(path.substr(start, slash - start));
        start = slash + 1;
    }
    parts.push_back(path.substr(start));
    return parts;
}

/// Follows `relative` from the folder of `link` (both mapped, '/' separated).
std::string follow(const std::string& link, const std::string& relative) {
    std::vector<std::string> parts = split(link);
    parts.pop_back();
    for (const std::string& part : split(relative)) {
        if (part.empty()) {
            std::abort(); // targets have no empty components
        }
        if (part == "..") {
            if (parts.empty()) {
                std::abort(); // targets never leave the destination
            }
            parts.pop_back();
        } else if (part != ".") {
            parts.push_back(part);
        }
    }
    std::string out;
    for (const std::string& part : parts) {
        out += out.empty() ? part : "/" + part;
    }
    return out;
}

void check_links(const transfer::ReceivePlan& plan) {
    constexpr std::size_t kTargets = 16; // bounds the work per input
    for (std::size_t link = 0; link < plan.entries.size(); ++link) {
        const protocol::ManifestEntry& entry = plan.entries[link].entry;
        if (entry.kind != protocol::EntryKind::Link) {
            continue;
        }
        const auto depth = static_cast<std::size_t>(std::ranges::count(entry.path, '/'));
        std::string up;
        for (std::size_t i = 0; i < depth; ++i) {
            up += "../";
        }
        for (std::size_t target = 0; target < std::min(plan.entries.size(), kTargets); ++target) {
            const auto mapped =
                transfer::mapped_link_target(plan, link, up + plan.entries[target].entry.path);
            if (mapped &&
                follow(plan.entries[link].target, *mapped) != plan.entries[target].target) {
                std::abort();
            }
        }
    }
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);
    osdrop::fuzz::check_round_trip(input, protocol::decode_transfer, protocol::encode_transfer);
    const auto decoded = protocol::decode_transfer(input);
    if (!decoded || !std::holds_alternative<protocol::Manifest>(decoded.value())) {
        return 0;
    }
    for (const auto target : {transfer::TargetPlatform::MacOS, transfer::TargetPlatform::Windows}) {
        transfer::ManifestCollector collector(target);
        if (collector.add(std::get<protocol::Manifest>(decoded.value()))) {
            check_links(collector.plan());
        }
    }
    return 0;
}
