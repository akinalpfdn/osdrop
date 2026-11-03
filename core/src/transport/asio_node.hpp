#pragma once

// Runs a Mesh over real sockets (DEVPLAN: "core owns one I/O thread"): one TCP listener for control
// connections and transfer streams, one UDP input socket per session, and a timer set to the
// mesh's next wakeup. Observer callbacks run on the I/O thread. Commands may come from any other
// thread and run on the I/O thread in order; they must not be called from an observer callback.

#include "crypto/noise.hpp"
#include "mesh/mesh.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/input_messages.hpp"
#include "protocol/transfer_messages.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace osdrop::transport {

struct NodeOptions {
    /// TCP port to listen on (0: any). Reusing the previous run's port keeps a Bonjour
    /// advertisement right; a port that is taken falls back to any.
    std::uint16_t listen_port = 0;
};

class AsioNode {
public:
    /// Listens on both IPv4 and IPv6 and starts the I/O thread. `attach` sees the mesh before
    /// anything reaches it, so a layer above (the group) can keep a reference to it.
    [[nodiscard]] static Expected<std::unique_ptr<AsioNode>, Error>
    create(mesh::MeshConfig config, crypto::KeyPair identity, crypto::RandomSource& random,
           mesh::MeshObserver& observer, NodeOptions options = {},
           const std::function<void(mesh::Mesh&)>& attach = {});

    /// Ends every session with Bye (application quit), lets queued bytes go out for a moment and
    /// joins the I/O thread.
    ~AsioNode();
    AsioNode(const AsioNode&) = delete;
    AsioNode& operator=(const AsioNode&) = delete;
    AsioNode(AsioNode&&) = delete;
    AsioNode& operator=(AsioNode&&) = delete;

    [[nodiscard]] std::uint16_t listen_port() const;
    [[nodiscard]] mesh::DeviceId device_id() const;

    // Queued: return at once.
    void peer_discovered(const mesh::DeviceId& device, const protocol::Id& group,
                         const mesh::PeerAddress& address);
    void peer_lost(const mesh::DeviceId& device);
    void send_motion(const mesh::DeviceId& device, const protocol::PointerMotion& motion);
    void close_transfer(const mesh::DeviceId& device, std::uint32_t stream_id);
    void close_all(protocol::ByeReason reason);

    /// Runs `work` on the I/O thread with the mesh, for layers above it; returns at once.
    void run(std::function<void(mesh::Mesh&, TimePoint)> work);
    /// As run, and waits until it ran; false when the I/O thread did not answer in time.
    [[nodiscard]] bool run_and_wait(std::function<void(mesh::Mesh&, TimePoint)> work);

    // Wait for the I/O thread's answer.
    [[nodiscard]] Expected<mesh::ConnectionId, Error> pair_with(const mesh::PeerAddress& address);
    [[nodiscard]] Status<Error> confirm_pairing(mesh::ConnectionId connection, bool accepted);
    [[nodiscard]] Status<Error> send_message(const mesh::DeviceId& device,
                                             const protocol::ControlMessage& message);
    [[nodiscard]] Expected<std::uint32_t, Error> open_transfer(const mesh::DeviceId& device);
    [[nodiscard]] Status<Error> send_transfer(const mesh::DeviceId& device, std::uint32_t stream_id,
                                              const protocol::TransferMessage& message);

    class Impl;

private:
    explicit AsioNode(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace osdrop::transport
