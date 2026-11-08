#pragma once

// The transfer streams of one computer, for the sending and the receiving side: a queue per
// stream that keeps messages while the mesh refuses more (backpressure), and the mesh calls made
// as deferred work, as the mesh requires of its observers.

#include "mesh/mesh.hpp"
#include "protocol/transfer_messages.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>

namespace osdrop::transfer {

struct StreamKey {
    mesh::DeviceId device{};
    std::uint32_t id = 0;
    auto operator<=>(const StreamKey&) const = default;
};

class StreamsListener {
public:
    virtual ~StreamsListener() = default;
    /// Queued messages went out; `queued` bytes are still waiting.
    virtual void on_pumped(const StreamKey& key, std::size_t queued) = 0;
};

class Streams {
public:
    explicit Streams(StreamsListener& listener) : listener_(listener) {}
    ~Streams() = default;
    Streams(const Streams&) = delete;
    Streams& operator=(const Streams&) = delete;
    Streams(Streams&&) = delete;
    Streams& operator=(Streams&&) = delete;

    /// The mesh to send through; set once, before it delivers anything.
    void attach(mesh::Mesh& mesh) { mesh_ = &mesh; }

    /// Opens a stream to `device`; `opened` runs with its ID, or the error, during the mesh's
    /// deferred work.
    void open(const mesh::DeviceId& device,
              std::function<void(Expected<std::uint32_t, Error>)> opened);
    /// A stream the peer opened.
    void accepted(const StreamKey& key);

    /// Queues a message; it goes out as soon as the stream takes it.
    void send(const StreamKey& key, protocol::TransferMessage message);
    /// Drops queued messages of one request that have not left yet.
    void drop(const StreamKey& key, std::uint32_t request_id);
    [[nodiscard]] std::size_t queued_bytes(const StreamKey& key) const;
    void pause(const StreamKey& key, bool paused);
    /// Closes the stream; with `flush`, after the queued messages went out.
    void close(const StreamKey& key, bool flush);

    // --- From the mesh ---
    void writable(const StreamKey& key);
    void closed(const StreamKey& key);

private:
    struct Outbox {
        std::deque<protocol::TransferMessage> messages;
        std::size_t bytes = 0;
        bool scheduled = false; ///< a pump is deferred
        bool closing = false;
    };

    void schedule(const StreamKey& key, Outbox& box);
    void pump(mesh::Mesh& mesh, const StreamKey& key, TimePoint now);

    StreamsListener& listener_;
    mesh::Mesh* mesh_ = nullptr; // set by attach(); outlived by the mesh's owner
    std::map<StreamKey, Outbox> outboxes_;
};

/// Bytes a message takes in a stream's queue, roughly as framed.
[[nodiscard]] std::size_t queued_size(const protocol::TransferMessage& message);

} // namespace osdrop::transfer
