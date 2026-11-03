#include "capi/random_source.hpp"
#include "crypto/noise.hpp"
#include "mesh/mesh.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/wire.hpp"
#include "transport/asio_node.hpp"

#include <osdrop/osdrop.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <string_view>

namespace {

namespace mesh = osdrop::mesh;
namespace protocol = osdrop::protocol;
using osdrop::Error;

static_assert(OSDROP_LOG_DEBUG == static_cast<int>(mesh::LogLevel::Debug) &&
              OSDROP_LOG_ERROR == static_cast<int>(mesh::LogLevel::Error));
static_assert(OSDROP_TRUST_TRUSTED == static_cast<int>(mesh::TrustDecision::Trusted) &&
              OSDROP_TRUST_UNKNOWN == static_cast<int>(mesh::TrustDecision::Unknown) &&
              OSDROP_TRUST_BLOCKED == static_cast<int>(mesh::TrustDecision::Blocked));

osdrop_result to_result(Error error) {
    return static_cast<osdrop_result>(error);
}

protocol::Id read_id(const std::uint8_t* bytes) {
    protocol::Id id{};
    std::copy_n(bytes, id.size(), id.begin());
    return id;
}

osdrop_peer describe(const osdrop::crypto::Key& key, const protocol::Hello& hello) {
    osdrop_peer peer{.device_id = {},
                     .public_key = {},
                     .platform = static_cast<osdrop_platform>(hello.platform),
                     .protocol_minor = hello.protocol_minor,
                     .has_group = static_cast<std::uint8_t>(!protocol::is_zero(hello.group_id)),
                     .group_id = {},
                     .name = {}};
    const auto device = osdrop::crypto::device_id(key);
    std::ranges::copy(device, std::begin(peer.device_id));
    std::ranges::copy(key, std::begin(peer.public_key));
    std::ranges::copy(hello.group_id, std::begin(peer.group_id));
    const std::size_t length = std::min(hello.device_name.size(), sizeof(peer.name) - 1);
    std::copy_n(hello.device_name.data(), length, std::begin(peer.name));
    return peer;
}

/// Turns mesh events into the platform's C callbacks.
class CallbackObserver final : public mesh::MeshObserver {
public:
    explicit CallbackObserver(const osdrop_mesh_callbacks& callbacks) : callbacks_(callbacks) {}

    mesh::TrustDecision on_trust_query(const osdrop::crypto::Key& key,
                                       const protocol::Hello& hello) override {
        const osdrop_peer peer = describe(key, hello);
        const osdrop_trust trust = callbacks_.trust(callbacks_.context, &peer);
        // Anything but a documented answer is a refusal.
        return trust == OSDROP_TRUST_TRUSTED || trust == OSDROP_TRUST_UNKNOWN
                   ? static_cast<mesh::TrustDecision>(trust)
                   : mesh::TrustDecision::Blocked;
    }
    void on_pairing_code(mesh::ConnectionId connection, std::uint32_t code,
                         const osdrop::crypto::Key& key, const protocol::Hello& hello) override {
        if (callbacks_.pairing_code != nullptr) {
            const osdrop_peer peer = describe(key, hello);
            callbacks_.pairing_code(callbacks_.context, connection, code, &peer);
        }
    }
    void on_paired(const osdrop::crypto::Key& key, const protocol::Hello& hello) override {
        if (callbacks_.paired != nullptr) {
            const osdrop_peer peer = describe(key, hello);
            callbacks_.paired(callbacks_.context, &peer);
        }
    }
    void on_pairing_failed(mesh::ConnectionId connection, Error reason) override {
        if (callbacks_.pairing_failed != nullptr) {
            callbacks_.pairing_failed(callbacks_.context, connection, to_result(reason));
        }
    }
    void on_peer_connected(const mesh::DeviceId& /*device*/, const osdrop::crypto::Key& key,
                           const protocol::Hello& hello,
                           const protocol::Capabilities& capabilities) override {
        if (callbacks_.peer_connected != nullptr) {
            const osdrop_peer peer = describe(key, hello);
            callbacks_.peer_connected(callbacks_.context, &peer, capabilities.features);
        }
    }
    void on_peer_disconnected(const mesh::DeviceId& device, std::optional<Error> reason) override {
        if (callbacks_.peer_disconnected != nullptr) {
            callbacks_.peer_disconnected(callbacks_.context, device.data(),
                                         reason ? to_result(*reason) : OSDROP_OK);
        }
    }
    void on_peer_responsive(const mesh::DeviceId& device, bool responsive) override {
        if (callbacks_.peer_responsive != nullptr) {
            callbacks_.peer_responsive(callbacks_.context, device.data(),
                                       static_cast<std::uint8_t>(responsive));
        }
    }
    // Messages, motion and transfer streams are consumed inside core (Phases 8-14).
    void on_message(const mesh::DeviceId& /*device*/,
                    const protocol::ControlMessage& /*message*/) override {}
    void on_motion(const mesh::DeviceId& /*device*/,
                   const protocol::PointerMotion& /*motion*/) override {}
    void on_transfer_opened(const mesh::DeviceId& /*device*/, std::uint32_t /*stream*/) override {}
    void on_transfer_message(const mesh::DeviceId& /*device*/, std::uint32_t /*stream*/,
                             const protocol::TransferMessage& /*message*/) override {}
    void on_transfer_writable(const mesh::DeviceId& /*device*/, std::uint32_t /*stream*/) override {
    }
    void on_transfer_closed(const mesh::DeviceId& /*device*/, std::uint32_t /*stream*/,
                            std::optional<Error> /*reason*/) override {}
    void on_stats(const mesh::DeviceId& device,
                  const osdrop::session::SessionStats& stats) override {
        if (callbacks_.stats != nullptr) {
            const osdrop_session_stats c_stats{.rtt_us = stats.rtt_us,
                                               .clock_offset_us = stats.clock_offset_us,
                                               .datagrams_rejected = stats.datagrams_rejected,
                                               .stale_motion_dropped = stats.stale_motion_dropped};
            callbacks_.stats(callbacks_.context, device.data(), &c_stats);
        }
    }
    void on_log(mesh::LogLevel level, std::string_view message) override {
        if (callbacks_.log != nullptr) {
            const std::string line(message);
            callbacks_.log(callbacks_.context, static_cast<osdrop_log_level>(level), line.c_str());
        }
    }

private:
    osdrop_mesh_callbacks callbacks_;
};

bool valid_platform(osdrop_platform platform) {
    return platform == OSDROP_PLATFORM_MACOS || platform == OSDROP_PLATFORM_WINDOWS;
}

} // namespace

/// The C handle: the platform's callbacks and the node that runs the mesh.
struct osdrop_mesh {
    osdrop_mesh(const osdrop_random_source& random_source, const osdrop_mesh_callbacks& callbacks)
        : random(random_source), observer(callbacks) {}

    osdrop::capi::CallbackRandomSource random;
    CallbackObserver observer;
    std::unique_ptr<osdrop::transport::AsioNode> node; // declared last: stopped first
};

extern "C" {

osdrop_result osdrop_mesh_create(const osdrop_mesh_config* config,
                                 const osdrop_random_source* random,
                                 const osdrop_mesh_callbacks* callbacks, osdrop_mesh** out_mesh) {
    if (config == nullptr || config->secret_key == nullptr || config->device_name == nullptr ||
        random == nullptr || random->fill == nullptr || callbacks == nullptr ||
        callbacks->trust == nullptr || out_mesh == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    *out_mesh = nullptr;
    if (!valid_platform(config->platform)) {
        return OSDROP_ERROR_VALUE_OUT_OF_RANGE;
    }
    // At most one byte past the longest valid name is read; longer names fail the check.
    std::string name;
    for (const char* c = config->device_name;
         *c != '\0' && name.size() <= protocol::kMaxStringBytes; ++c) {
        name.push_back(*c);
    }
    if (const auto valid = protocol::check_string(name); !valid) {
        return to_result(valid.error());
    }
    mesh::MeshConfig mesh_config;
    mesh_config.platform = static_cast<protocol::Platform>(config->platform);
    mesh_config.device_name = name;
    if (config->group_id != nullptr) {
        mesh_config.group_id = read_id(config->group_id);
    }
    mesh_config.capabilities = protocol::Capabilities{
        .features = config->features, .max_transfer_streams = config->max_transfer_streams};
    if (const auto encoded = protocol::encode_control(mesh_config.capabilities); !encoded) {
        return to_result(encoded.error());
    }

    osdrop::crypto::Key secret{};
    std::copy_n(config->secret_key, secret.size(), secret.begin());
    auto identity = osdrop::crypto::KeyPair::from_secret(secret);
    osdrop::crypto::wipe(secret);
    if (!identity) {
        return to_result(identity.error());
    }

    auto handle = std::unique_ptr<osdrop_mesh>(new (std::nothrow) osdrop_mesh(*random, *callbacks));
    if (!handle) {
        return OSDROP_ERROR_INTERNAL;
    }
    auto node = osdrop::transport::AsioNode::create(
        std::move(mesh_config), std::move(identity.value()), handle->random, handle->observer,
        osdrop::transport::NodeOptions{.listen_port = config->listen_port});
    if (!node) {
        return to_result(node.error());
    }
    handle->node = std::move(node.value());
    *out_mesh = handle.release();
    return OSDROP_OK;
}

void osdrop_mesh_destroy(osdrop_mesh* mesh) {
    delete mesh; // NOLINT(cppcoreguidelines-owning-memory): the C API owns the handle
}

osdrop_result osdrop_mesh_listen_port(const osdrop_mesh* mesh, uint16_t* out_port) {
    if (mesh == nullptr || out_port == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    *out_port = mesh->node->listen_port();
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_device_id(const osdrop_mesh* mesh, uint8_t* out_device_id) {
    if (mesh == nullptr || out_device_id == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    std::ranges::copy(mesh->node->device_id(), out_device_id);
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_peer_discovered(osdrop_mesh* mesh, const uint8_t* device_id,
                                          const uint8_t* group_id, const char* host,
                                          uint16_t port) {
    if (mesh == nullptr || device_id == nullptr || host == nullptr || host[0] == '\0' ||
        port == 0) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    mesh->node->peer_discovered(read_id(device_id),
                                group_id != nullptr ? read_id(group_id) : protocol::Id{},
                                mesh::PeerAddress{.host = host, .port = port});
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_peer_lost(osdrop_mesh* mesh, const uint8_t* device_id) {
    if (mesh == nullptr || device_id == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    mesh->node->peer_lost(read_id(device_id));
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_set_group(osdrop_mesh* mesh, const uint8_t* group_id) {
    if (mesh == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    mesh->node->set_group(group_id != nullptr ? read_id(group_id) : protocol::Id{});
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_pair(osdrop_mesh* mesh, const char* host, uint16_t port,
                               uint64_t* out_connection) {
    if (mesh == nullptr || host == nullptr || host[0] == '\0' || port == 0 ||
        out_connection == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const auto connection = mesh->node->pair_with(mesh::PeerAddress{.host = host, .port = port});
    if (!connection) {
        return to_result(connection.error());
    }
    *out_connection = connection.value();
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_confirm_pairing(osdrop_mesh* mesh, uint64_t connection,
                                          uint8_t accepted) {
    if (mesh == nullptr || accepted > 1) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const auto confirmed = mesh->node->confirm_pairing(connection, accepted == 1);
    return confirmed ? OSDROP_OK : to_result(confirmed.error());
}

osdrop_result osdrop_mesh_disconnect(osdrop_mesh* mesh, const uint8_t* device_id) {
    if (mesh == nullptr || device_id == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    mesh->node->disconnect(read_id(device_id), protocol::ByeReason::UserDisconnected);
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_close_all(osdrop_mesh* mesh, uint8_t sleeping) {
    if (mesh == nullptr || sleeping > 1) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    mesh->node->close_all(sleeping == 1 ? protocol::ByeReason::Sleeping
                                        : protocol::ByeReason::ApplicationQuit);
    return OSDROP_OK;
}

} // extern "C"
