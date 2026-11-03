#pragma once

// Transfer stream messages (docs/PROTOCOL.md, "Transfer streams").

#include "protocol/constants.hpp"
#include "protocol/error.hpp"
#include "protocol/wire.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace osdrop::protocol {

enum class TransferType : std::uint8_t {
    TransferHello = 0x01,
    ManifestRequest = 0x02,
    Manifest = 0x03,
    ContentRequest = 0x04,
    ContentChunk = 0x05,
    ContentEnd = 0x06,
    TransferError = 0x07,
    TransferCancel = 0x08,
};

enum class ObjectKind : std::uint8_t { Drag = 1, Clipboard = 2 };
enum class EntryKind : std::uint8_t { File = 1, Folder = 2, Link = 3 };

inline constexpr std::uint64_t kMaxLinkTarget = 4096;
inline constexpr std::uint32_t kFilesClipboardFormat = 5;

struct TransferHello {
    static constexpr TransferType kType = TransferType::TransferHello;
    Id transfer_token{};
    std::uint32_t stream_id = 0;
    bool operator==(const TransferHello&) const = default;
};

/// What a request refers to: a dragged item, or one format of a clipboard announcement.
struct ObjectRef {
    ObjectKind kind = ObjectKind::Drag;
    std::uint64_t object_id = 0;
    std::uint32_t item_index = 0;
    bool operator==(const ObjectRef&) const = default;
};

struct ManifestRequest {
    static constexpr TransferType kType = TransferType::ManifestRequest;
    std::uint32_t request_id = 0;
    ObjectRef object;
    bool operator==(const ManifestRequest&) const = default;
};

struct ManifestEntry {
    std::string path;
    EntryKind kind = EntryKind::File;
    bool executable = false;
    std::uint64_t byte_count = 0;
    std::int64_t modified_us = 0;
    bool operator==(const ManifestEntry&) const = default;
};

struct Manifest {
    static constexpr TransferType kType = TransferType::Manifest;
    std::uint32_t request_id = 0;
    bool is_last = false;
    std::vector<ManifestEntry> entries;
    bool operator==(const Manifest&) const = default;
};

struct ContentRequest {
    static constexpr TransferType kType = TransferType::ContentRequest;
    std::uint32_t request_id = 0;
    ObjectRef object;
    std::uint32_t entry_index = 0;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    bool operator==(const ContentRequest&) const = default;
};

struct ContentChunk {
    static constexpr TransferType kType = TransferType::ContentChunk;
    std::uint32_t request_id = 0;
    std::uint64_t offset = 0;
    std::vector<std::uint8_t> data;
    bool operator==(const ContentChunk&) const = default;
};

struct ContentEnd {
    static constexpr TransferType kType = TransferType::ContentEnd;
    std::uint32_t request_id = 0;
    std::uint64_t byte_count = 0;
    Digest digest{};
    bool operator==(const ContentEnd&) const = default;
};

struct TransferError {
    static constexpr TransferType kType = TransferType::TransferError;
    std::uint32_t request_id = 0; ///< 0: the whole stream
    std::uint16_t code = 0;
    bool operator==(const TransferError&) const = default;
};

struct TransferCancel {
    static constexpr TransferType kType = TransferType::TransferCancel;
    std::uint32_t request_id = 0;
    bool operator==(const TransferCancel&) const = default;
};

using TransferMessage = std::variant<TransferHello, ManifestRequest, Manifest, ContentRequest,
                                     ContentChunk, ContentEnd, TransferError, TransferCancel>;

[[nodiscard]] TransferType type_of(const TransferMessage& message);

[[nodiscard]] Expected<TransferMessage, Error> decode_transfer(std::span<const std::uint8_t> bytes);

[[nodiscard]] Expected<std::vector<std::uint8_t>, Error>
encode_transfer(const TransferMessage& message);

} // namespace osdrop::protocol
