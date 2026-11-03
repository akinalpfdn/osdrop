#include "protocol/byte_io.hpp"
#include "protocol/transfer_messages.hpp"
#include "protocol/wire.hpp"

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace osdrop::protocol {
namespace {

using Decoded = Expected<TransferMessage, Error>;

// The standard visitor idiom: one call operator per lambda.
template <typename... Visitors>
struct Overloaded : Visitors... { // NOLINT(misc-multiple-inheritance)
    using Visitors::operator()...;
};

Decoded decode_failure(Error error) {
    return Decoded::failure(error);
}

Decoded finish(const FieldReader& f, TransferMessage message) {
    if (!f.reserved_zero()) {
        return decode_failure(Error::ValueOutOfRange);
    }
    return message;
}

// --- Validation, shared by encoder and decoder ---------------------------------------------------

bool is_valid(const ObjectRef& object) {
    if (object.object_id == 0) {
        return false;
    }
    switch (object.kind) {
    case ObjectKind::Drag:
        return object.item_index < kMaxDragItems;
    case ObjectKind::Clipboard:
        return in_range(object.item_index, 1, kMaxClipboardFormats);
    }
    return false;
}

Check validate(const TransferHello& m) {
    return range(!is_zero(m.transfer_token) && m.stream_id != 0);
}

Check validate(const ManifestRequest& m) {
    return range(m.request_id != 0 && is_valid(m.object));
}

Check validate(const ManifestEntry& entry) {
    bool valid = false;
    switch (entry.kind) {
    case EntryKind::File:
        valid = true;
        break;
    case EntryKind::Folder:
        valid = !entry.executable && entry.byte_count == 0;
        break;
    case EntryKind::Link:
        valid = !entry.executable && in_range(entry.byte_count, 1, kMaxLinkTarget);
        break;
    }
    if (!valid) {
        return fail(Error::ValueOutOfRange);
    }
    return check_path(entry.path);
}

Check validate(const Manifest& m) {
    if (m.request_id == 0 || !in_range(m.entries.size(), 1, kMaxManifestEntries)) {
        return fail(Error::ValueOutOfRange);
    }
    for (const ManifestEntry& entry : m.entries) {
        if (const Check valid = validate(entry); !valid) {
            return valid;
        }
    }
    return ok();
}

Check validate(const ContentRequest& m) {
    // Clipboard formats other than files are single contents without a manifest.
    const bool single_content =
        m.object.kind == ObjectKind::Clipboard && m.object.item_index != kFilesClipboardFormat;
    return range(m.request_id != 0 && is_valid(m.object) && m.length != 0 &&
                 (!single_content || m.entry_index == 0));
}

Check validate(const ContentChunk& m) {
    return range(m.request_id != 0 && in_range(m.data.size(), 1, kMaxChunkData));
}

Check validate(const ContentEnd& m) {
    return range(m.request_id != 0);
}

Check validate(const TransferError& m) {
    return range(m.code != 0);
}

Check validate(const TransferCancel& m) {
    return range(m.request_id != 0);
}

// --- Decoding ------------------------------------------------------------------------------------

bool read_object(FieldReader& f, ObjectRef& out) {
    return f.enumeration(out.kind) && f.reserved(3) && f.raw().u64(out.object_id) &&
           f.raw().u32(out.item_index);
}

Decoded read_transfer_hello(FieldReader& f) {
    TransferHello m;
    if (!read_array(f.raw(), m.transfer_token) || !f.raw().u32(m.stream_id)) {
        return decode_failure(Error::Truncated);
    }
    return finish(f, m);
}

Decoded read_manifest_request(FieldReader& f) {
    ManifestRequest m;
    if (!f.raw().u32(m.request_id) || !read_object(f, m.object)) {
        return decode_failure(Error::Truncated);
    }
    return finish(f, m);
}

Decoded read_manifest(FieldReader& f) {
    Manifest m;
    Reader& r = f.raw();
    std::uint8_t is_last = 0;
    std::uint16_t count = 0;
    if (!r.u32(m.request_id) || !r.u8(is_last) || !f.reserved(1) || !r.u16(count)) {
        return decode_failure(Error::Truncated);
    }
    if (is_last > 1 || count == 0 || count > kMaxManifestEntries) {
        return decode_failure(Error::ValueOutOfRange);
    }
    m.is_last = is_last == 1;
    m.entries.resize(count);
    for (ManifestEntry& entry : m.entries) {
        std::uint8_t flags = 0;
        if (!read_long_string(r, entry.path) || !f.enumeration(entry.kind) || !f.flags(1, flags) ||
            !f.reserved(2) || !r.u64(entry.byte_count) || !r.i64(entry.modified_us)) {
            return decode_failure(Error::Truncated);
        }
        entry.executable = (flags & 1U) != 0;
    }
    return finish(f, std::move(m));
}

Decoded read_content_request(FieldReader& f) {
    ContentRequest m;
    Reader& r = f.raw();
    if (!r.u32(m.request_id) || !read_object(f, m.object) || !r.u32(m.entry_index) ||
        !r.u64(m.offset) || !r.u64(m.length)) {
        return decode_failure(Error::Truncated);
    }
    return finish(f, m);
}

Decoded read_content_chunk(FieldReader& f) {
    ContentChunk m;
    Reader& r = f.raw();
    std::uint32_t length = 0;
    std::span<const std::uint8_t> data;
    if (!r.u32(m.request_id) || !r.u64(m.offset) || !r.u32(length) || !r.bytes(length, data)) {
        return decode_failure(Error::Truncated);
    }
    m.data.assign(data.begin(), data.end());
    return finish(f, std::move(m));
}

Decoded read_content_end(FieldReader& f) {
    ContentEnd m;
    if (!f.raw().u32(m.request_id) || !f.raw().u64(m.byte_count) ||
        !read_array(f.raw(), m.digest)) {
        return decode_failure(Error::Truncated);
    }
    return finish(f, m);
}

Decoded read_transfer_error(FieldReader& f) {
    TransferError m;
    if (!f.raw().u32(m.request_id) || !f.raw().u16(m.code)) {
        return decode_failure(Error::Truncated);
    }
    return finish(f, m);
}

Decoded read_transfer_cancel(FieldReader& f) {
    TransferCancel m;
    if (!f.raw().u32(m.request_id)) {
        return decode_failure(Error::Truncated);
    }
    return finish(f, m);
}

Decoded read_body(std::uint8_t type, FieldReader& f) {
    switch (static_cast<TransferType>(type)) {
    case TransferType::TransferHello:
        return read_transfer_hello(f);
    case TransferType::ManifestRequest:
        return read_manifest_request(f);
    case TransferType::Manifest:
        return read_manifest(f);
    case TransferType::ContentRequest:
        return read_content_request(f);
    case TransferType::ContentChunk:
        return read_content_chunk(f);
    case TransferType::ContentEnd:
        return read_content_end(f);
    case TransferType::TransferError:
        return read_transfer_error(f);
    case TransferType::TransferCancel:
        return read_transfer_cancel(f);
    }
    return decode_failure(Error::UnknownType);
}

// --- Encoding ------------------------------------------------------------------------------------

void write_object(Writer& w, const ObjectRef& object) {
    write_enum(w, object.kind);
    write_reserved(w, 3);
    w.u64(object.object_id);
    w.u32(object.item_index);
}

void write_body(Writer& w, const TransferMessage& message) {
    std::visit(Overloaded{
                   [&](const TransferHello& m) {
                       w.bytes(m.transfer_token);
                       w.u32(m.stream_id);
                   },
                   [&](const ManifestRequest& m) {
                       w.u32(m.request_id);
                       write_object(w, m.object);
                   },
                   [&](const Manifest& m) {
                       w.u32(m.request_id);
                       w.u8(static_cast<std::uint8_t>(m.is_last));
                       write_reserved(w, 1);
                       w.u16(static_cast<std::uint16_t>(m.entries.size()));
                       for (const ManifestEntry& entry : m.entries) {
                           write_long_string(w, entry.path);
                           write_enum(w, entry.kind);
                           w.u8(static_cast<std::uint8_t>(entry.executable));
                           write_reserved(w, 2);
                           w.u64(entry.byte_count);
                           w.i64(entry.modified_us);
                       }
                   },
                   [&](const ContentRequest& m) {
                       w.u32(m.request_id);
                       write_object(w, m.object);
                       w.u32(m.entry_index);
                       w.u64(m.offset);
                       w.u64(m.length);
                   },
                   [&](const ContentChunk& m) {
                       w.u32(m.request_id);
                       w.u64(m.offset);
                       w.u32(static_cast<std::uint32_t>(m.data.size()));
                       w.bytes(m.data);
                   },
                   [&](const ContentEnd& m) {
                       w.u32(m.request_id);
                       w.u64(m.byte_count);
                       w.bytes(m.digest);
                   },
                   [&](const TransferError& m) {
                       w.u32(m.request_id);
                       w.u16(m.code);
                   },
                   [&](const TransferCancel& m) { w.u32(m.request_id); },
               },
               message);
}

} // namespace

TransferType type_of(const TransferMessage& message) {
    return std::visit([](const auto& m) { return std::remove_cvref_t<decltype(m)>::kType; },
                      message);
}

Expected<TransferMessage, Error> decode_transfer(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > kMaxTransferMessageSize) {
        return decode_failure(Error::TooLarge);
    }
    Reader reader(bytes);
    std::uint8_t type = 0;
    if (const Check prefix = read_prefix(reader, type); !prefix) {
        return decode_failure(prefix.error());
    }
    FieldReader fields(reader);
    auto decoded = read_body(type, fields);
    if (!decoded) {
        return decoded;
    }
    if (reader.remaining() != 0) {
        return decode_failure(Error::TrailingBytes);
    }
    const Check valid = std::visit([](const auto& m) { return validate(m); }, decoded.value());
    if (!valid) {
        return decode_failure(valid.error());
    }
    return decoded;
}

Expected<std::vector<std::uint8_t>, Error> encode_transfer(const TransferMessage& message) {
    using Result = Expected<std::vector<std::uint8_t>, Error>;
    const Check valid = std::visit([](const auto& m) { return validate(m); }, message);
    if (!valid) {
        return Result::failure(valid.error());
    }
    std::vector<std::uint8_t> buffer(kMaxTransferMessageSize);
    Writer writer(buffer);
    write_prefix(writer, static_cast<std::uint8_t>(type_of(message)));
    write_body(writer, message);
    if (!writer.ok()) {
        return Result::failure(Error::TooLarge);
    }
    buffer.resize(writer.size());
    return buffer;
}

} // namespace osdrop::protocol
