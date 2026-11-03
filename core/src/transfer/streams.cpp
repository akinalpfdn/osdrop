#include "transfer/streams.hpp"

#include "protocol/constants.hpp"
#include "transfer/manifest.hpp"

#include <algorithm>
#include <utility>
#include <variant>

namespace osdrop::transfer {
namespace {

constexpr std::size_t kFramingSize = 4 + protocol::kAeadTagSize; // length prefix and tag
constexpr std::size_t kSmallMessageSize = 64;

std::uint32_t request_of(const protocol::TransferMessage& message) {
    return std::visit(
        [](const auto& m) -> std::uint32_t {
            if constexpr (requires { m.request_id; }) {
                return m.request_id;
            } else {
                return 0;
            }
        },
        message);
}

} // namespace

std::size_t queued_size(const protocol::TransferMessage& message) {
    if (const auto* chunk = std::get_if<protocol::ContentChunk>(&message)) {
        return kFramingSize + protocol::kHeaderSize + protocol::kContentChunkFieldsSize +
               chunk->data.size();
    }
    if (const auto* manifest = std::get_if<protocol::Manifest>(&message)) {
        return kFramingSize + manifest_size(manifest->entries);
    }
    return kSmallMessageSize;
}

void Streams::open(const mesh::DeviceId& device,
                   std::function<void(Expected<std::uint32_t, Error>)> opened) {
    if (mesh_ == nullptr) {
        opened(Expected<std::uint32_t, Error>::failure(Error::InvalidState));
        return;
    }
    mesh_->defer([this, device, opened = std::move(opened)](mesh::Mesh& mesh, TimePoint now) {
        auto stream = mesh.open_transfer(device, now);
        if (stream) {
            outboxes_.try_emplace(StreamKey{.device = device, .id = stream.value()});
        }
        opened(stream);
    });
}

void Streams::accepted(const StreamKey& key) {
    outboxes_.try_emplace(key);
}

void Streams::send(const StreamKey& key, protocol::TransferMessage message) {
    const auto it = outboxes_.find(key);
    if (it == outboxes_.end() || it->second.closing) {
        return;
    }
    it->second.bytes += queued_size(message);
    it->second.messages.push_back(std::move(message));
    schedule(key, it->second);
}

void Streams::drop(const StreamKey& key, std::uint32_t request_id) {
    const auto it = outboxes_.find(key);
    if (it == outboxes_.end()) {
        return;
    }
    Outbox& box = it->second;
    std::erase_if(box.messages, [&](const protocol::TransferMessage& message) {
        if (request_of(message) != request_id) {
            return false;
        }
        box.bytes -= queued_size(message);
        return true;
    });
}

std::size_t Streams::queued_bytes(const StreamKey& key) const {
    const auto it = outboxes_.find(key);
    return it == outboxes_.end() ? 0 : it->second.bytes;
}

void Streams::pause(const StreamKey& key, bool paused) {
    if (mesh_ == nullptr || !outboxes_.contains(key)) {
        return;
    }
    mesh_->defer([key, paused](mesh::Mesh& mesh, TimePoint now) {
        mesh.pause_transfer(key.device, key.id, paused, now);
    });
}

void Streams::close(const StreamKey& key, bool flush) {
    const auto it = outboxes_.find(key);
    if (it == outboxes_.end() || mesh_ == nullptr) {
        return;
    }
    if (flush && !it->second.messages.empty()) {
        it->second.closing = true;
        schedule(key, it->second);
        return;
    }
    outboxes_.erase(it);
    mesh_->defer(
        [key](mesh::Mesh& mesh, TimePoint now) { mesh.close_transfer(key.device, key.id, now); });
}

void Streams::writable(const StreamKey& key) {
    if (const auto it = outboxes_.find(key); it != outboxes_.end()) {
        schedule(key, it->second);
    }
}

void Streams::closed(const StreamKey& key) {
    outboxes_.erase(key);
}

void Streams::schedule(const StreamKey& key, Outbox& box) {
    if (box.scheduled || mesh_ == nullptr) {
        return;
    }
    box.scheduled = true;
    mesh_->defer([this, key](mesh::Mesh& mesh, TimePoint now) { pump(mesh, key, now); });
}

void Streams::pump(mesh::Mesh& mesh, const StreamKey& key, TimePoint now) {
    const auto it = outboxes_.find(key);
    if (it == outboxes_.end()) {
        return;
    }
    Outbox& box = it->second;
    box.scheduled = false;
    while (!box.messages.empty()) {
        // PEER_BUSY: wait for writable(). INVALID_STATE: not connected yet, or gone (closed()
        // follows).
        if (!mesh.send_transfer(key.device, key.id, box.messages.front(), now)) {
            break;
        }
        box.bytes -= queued_size(box.messages.front());
        box.messages.pop_front();
    }
    if (box.closing && box.messages.empty()) {
        outboxes_.erase(it);
        mesh.close_transfer(key.device, key.id, now);
        return;
    }
    listener_.on_pumped(key, box.bytes);
}

} // namespace osdrop::transfer
