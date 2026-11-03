#include "capi/random_source.hpp"
#include "crypto/noise.hpp"
#include "group/group.hpp"
#include "group/group_state.hpp"
#include "layout/document.hpp"
#include "layout/layout.hpp"
#include "layout/layout_state.hpp"
#include "layout/screens.hpp"
#include "mesh/mesh.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/wire.hpp"
#include "transport/asio_node.hpp"

#include <osdrop/osdrop.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <new>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace group = osdrop::group;
namespace layout = osdrop::layout;
namespace mesh = osdrop::mesh;
namespace protocol = osdrop::protocol;
using osdrop::Error;
using osdrop::TimePoint;

static_assert(OSDROP_LOG_DEBUG == static_cast<int>(mesh::LogLevel::Debug) &&
              OSDROP_LOG_ERROR == static_cast<int>(mesh::LogLevel::Error));
static_assert(OSDROP_LAYOUT_OVERLAP == static_cast<int>(layout::ProblemKind::Overlap) &&
              OSDROP_LAYOUT_UNREACHABLE == static_cast<int>(layout::ProblemKind::Unreachable));
static_assert(OSDROP_MONITOR_KEY_SIZE == protocol::kIdSize &&
              OSDROP_MAX_MONITORS == protocol::kMaxMonitors);

osdrop_result to_result(Error error) {
    return static_cast<osdrop_result>(error);
}

osdrop_result to_result(const osdrop::Status<Error>& status) {
    return status ? OSDROP_OK : to_result(status.error());
}

protocol::Id read_id(const std::uint8_t* bytes) {
    protocol::Id id{};
    std::copy_n(bytes, id.size(), id.begin());
    return id;
}

/// Copies as much of `name` as fits and terminates it.
void write_name(std::string_view name, std::span<char> out) {
    const std::size_t length = std::min(name.size(), out.size() - 1);
    std::ranges::copy(name.substr(0, length), out.begin());
    out[length] = '\0';
}

osdrop_peer describe(const osdrop::crypto::Key& key, const protocol::Hello& hello) {
    osdrop_peer peer{.device_id = {},
                     .public_key = {},
                     .platform = static_cast<osdrop_platform>(hello.platform),
                     .protocol_minor = hello.protocol_minor,
                     .has_group = static_cast<std::uint8_t>(!protocol::is_zero(hello.group_id)),
                     .group_id = {},
                     .name = {}};
    std::ranges::copy(osdrop::crypto::device_id(key), std::begin(peer.device_id));
    std::ranges::copy(key, std::begin(peer.public_key));
    std::ranges::copy(hello.group_id, std::begin(peer.group_id));
    write_name(hello.device_name, peer.name);
    return peer;
}

osdrop_member describe(const group::MemberStatus& status) {
    osdrop_member member{
        .device_id = {},
        .public_key = {},
        .platform = static_cast<osdrop_platform>(status.member.platform),
        .name = {},
        .permissions = {.control = static_cast<std::uint8_t>(status.permissions.control),
                        .files = static_cast<std::uint8_t>(status.permissions.files),
                        .clipboard = static_cast<std::uint8_t>(status.permissions.clipboard)},
        .self = static_cast<std::uint8_t>(status.self),
        .connected = static_cast<std::uint8_t>(status.connected)};
    std::ranges::copy(status.member.device_id, std::begin(member.device_id));
    std::ranges::copy(status.member.public_key, std::begin(member.public_key));
    write_name(status.member.name, member.name);
    return member;
}

std::set<layout::DeviceId> members_of(const group::GroupState& state) {
    std::set<layout::DeviceId> members;
    for (const group::Member& member : state.roster.members()) {
        members.insert(member.device_id);
    }
    return members;
}

/// Turns group and layout events, and the mesh events passed on for members, into the platform's
/// C callbacks, and tells the layout about group changes. The group answers trust and handles
/// pairing itself, so those mesh events never arrive here.
class CallbackObserver final : public group::GroupObserver,
                               public layout::LayoutObserver,
                               public mesh::MeshObserver {
public:
    explicit CallbackObserver(const osdrop_mesh_callbacks& callbacks) : callbacks_(callbacks) {}

    /// The layout to tell about group changes; set once, before the mesh starts.
    void follow(layout::Layout& arrangement) { layout_ = &arrangement; }

    // --- group::GroupObserver ---
    void on_state_changed(const group::GroupState& state) override {
        if (layout_ != nullptr) {
            layout_->group_changed(state.group_id, members_of(state));
        }
        if (callbacks_.group_changed != nullptr) {
            const std::vector<std::uint8_t> bytes = group::encode_state(state);
            callbacks_.group_changed(callbacks_.context, bytes.data(), bytes.size(),
                                     state.in_group() ? state.group_id.data() : nullptr);
        }
    }
    void on_pairing_code(mesh::ConnectionId connection, std::uint32_t code,
                         const osdrop::crypto::Key& key, const protocol::Hello& hello) override {
        if (callbacks_.pairing_code != nullptr) {
            const osdrop_peer peer = describe(key, hello);
            callbacks_.pairing_code(callbacks_.context, connection, code, &peer);
        }
    }
    void on_pairing_failed(mesh::ConnectionId connection, Error reason) override {
        if (callbacks_.pairing_failed != nullptr) {
            callbacks_.pairing_failed(callbacks_.context, connection, to_result(reason));
        }
    }
    void on_left_group(std::optional<Error> reason) override {
        if (callbacks_.left_group != nullptr) {
            callbacks_.left_group(callbacks_.context, reason ? to_result(*reason) : OSDROP_OK);
        }
    }

    // --- layout::LayoutObserver ---
    void on_layout_changed(const layout::LayoutState& state) override {
        if (callbacks_.layout_changed != nullptr) {
            const std::vector<std::uint8_t> bytes = layout::encode_layout(state);
            callbacks_.layout_changed(callbacks_.context, bytes.data(), bytes.size());
        }
    }

    // --- mesh::MeshObserver, for members ---
    mesh::TrustVerdict on_trust_query(const osdrop::crypto::Key& /*key*/,
                                      const protocol::Hello& /*hello*/) override {
        return {}; // never asked: refuse
    }
    void on_paired(const osdrop::crypto::Key& /*key*/, const protocol::Hello& /*hello*/) override {}
    void on_peer_connected(const mesh::DeviceId& /*device*/, const osdrop::crypto::Key& key,
                           const protocol::Hello& hello,
                           const protocol::Capabilities& capabilities) override {
        if (callbacks_.peer_connected != nullptr) {
            const osdrop_peer peer = describe(key, hello);
            callbacks_.peer_connected(callbacks_.context, &peer, capabilities.features);
        }
    }
    void on_peer_capabilities(const mesh::DeviceId& /*device*/,
                              const protocol::Capabilities& /*capabilities*/) override {}
    void on_peer_disconnected(const mesh::DeviceId& device, std::optional<Error> reason,
                              std::optional<protocol::ByeReason> /*bye*/) override {
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
    // Messages, motion and transfer streams are consumed inside core (Phases 9-15).
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
    layout::Layout* layout_ = nullptr;
};

bool valid_platform(osdrop_platform platform) {
    return platform == OSDROP_PLATFORM_MACOS || platform == OSDROP_PLATFORM_WINDOWS;
}

/// At most one byte past the longest valid name is read; longer names then fail the check.
std::string read_name(const char* text) {
    std::string name;
    for (const char* c = text; *c != '\0' && name.size() <= protocol::kMaxStringBytes; ++c) {
        name.push_back(*c);
    }
    return name;
}

layout::Rect to_rect(const osdrop_rect& rect) {
    return {.x = rect.x, .y = rect.y, .width = rect.width, .height = rect.height};
}

osdrop_rect to_c(const layout::Rect& rect) {
    return {.x = static_cast<std::int32_t>(rect.x),
            .y = static_cast<std::int32_t>(rect.y),
            .width = static_cast<std::int32_t>(rect.width),
            .height = static_cast<std::int32_t>(rect.height)};
}

/// Monitors a ScreenReport can carry, or why not.
osdrop::Expected<layout::Screens, Error> to_screens(const osdrop_monitor* monitors,
                                                    std::size_t count) {
    using Result = osdrop::Expected<layout::Screens, Error>;
    if (monitors == nullptr || count == 0) {
        return Result::failure(Error::InvalidArgument);
    }
    if (count > OSDROP_MAX_MONITORS) {
        return Result::failure(Error::ValueOutOfRange);
    }
    std::vector<layout::Monitor> list;
    for (const osdrop_monitor& m : std::span(monitors, count)) {
        if (m.primary > 1) {
            return Result::failure(Error::InvalidArgument);
        }
        layout::Monitor monitor{.key = {},
                                .frame = to_rect(m.frame),
                                .native = to_rect(m.native),
                                .pixel_width = m.pixel_width,
                                .pixel_height = m.pixel_height,
                                .primary = m.primary == 1};
        std::ranges::copy(m.key, monitor.key.begin());
        list.push_back(monitor);
    }
    layout::Screens screens(std::move(list));
    if (const auto valid = protocol::encode_control(screens.to_report()); !valid) {
        return Result::failure(valid.error());
    }
    return screens;
}

osdrop_monitor to_c(const layout::Monitor& monitor) {
    osdrop_monitor out{.key = {},
                       .frame = to_c(monitor.frame),
                       .native = to_c(monitor.native),
                       .pixel_width = monitor.pixel_width,
                       .pixel_height = monitor.pixel_height,
                       .primary = static_cast<std::uint8_t>(monitor.primary)};
    std::ranges::copy(monitor.key, std::begin(out.key));
    return out;
}

osdrop_layout_computer describe(const layout::DeviceId& device, const layout::Screens& screens,
                                const layout::Placements& placements) {
    osdrop_layout_computer computer{};
    std::ranges::copy(device, std::begin(computer.placement.device_id));
    if (const auto placed = placements.find(device); placed != placements.end()) {
        computer.placed = 1;
        computer.placement.x = static_cast<std::int32_t>(placed->second.offset.x);
        computer.placement.y = static_cast<std::int32_t>(placed->second.offset.y);
        computer.placement.detached = static_cast<std::uint8_t>(placed->second.detached);
    }
    const std::span<osdrop_monitor> out(computer.monitors);
    const auto& monitors = screens.monitors();
    computer.monitor_count = static_cast<std::uint8_t>(std::min(monitors.size(), out.size()));
    std::ranges::transform(monitors.begin(), monitors.begin() + computer.monitor_count, out.begin(),
                           [](const layout::Monitor& m) { return to_c(m); });
    return computer;
}

/// A state the platform stored, or the empty one when there is none yet.
template <typename State, typename Decode>
osdrop::Expected<State, Error> stored(const std::uint8_t* bytes, std::size_t size, Decode decode) {
    if (bytes == nullptr) {
        return State{};
    }
    return decode(std::span<const std::uint8_t>(bytes, size));
}

/// Placements with unique devices and detached flags of 0 or 1.
osdrop::Expected<layout::Placements, Error> to_placements(const osdrop_placement* placements,
                                                          std::size_t count) {
    using Result = osdrop::Expected<layout::Placements, Error>;
    layout::Placements result;
    for (const osdrop_placement& p : std::span(placements, count)) {
        if (p.detached > 1 ||
            !result
                 .emplace(read_id(p.device_id), layout::Placement{.offset = {.x = p.x, .y = p.y},
                                                                  .detached = p.detached == 1})
                 .second) {
            return Result::failure(Error::InvalidArgument);
        }
    }
    return result;
}

} // namespace

/// The C handle: the platform's callbacks, the group, the layout and the node that runs the mesh.
/// The group and the layout live on the I/O thread: every access goes through the node.
struct osdrop_mesh {
    osdrop_mesh(const osdrop_random_source& random_source, const osdrop_mesh_callbacks& callbacks)
        : random(random_source), observer(callbacks) {}

    osdrop::capi::CallbackRandomSource random;
    CallbackObserver observer;
    std::unique_ptr<layout::Layout> layout;
    std::unique_ptr<group::Group> group;
    std::unique_ptr<osdrop::transport::AsioNode> node; // declared last: stopped first
};

extern "C" {

osdrop_result osdrop_mesh_create(const osdrop_mesh_config* config,
                                 const osdrop_random_source* random,
                                 const osdrop_mesh_callbacks* callbacks, osdrop_mesh** out_mesh) {
    if (config == nullptr || config->secret_key == nullptr || config->device_name == nullptr ||
        (config->group_state == nullptr && config->group_state_size != 0) ||
        (config->layout_state == nullptr && config->layout_state_size != 0) || random == nullptr ||
        random->fill == nullptr || callbacks == nullptr || out_mesh == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    *out_mesh = nullptr;
    if (!valid_platform(config->platform)) {
        return OSDROP_ERROR_VALUE_OUT_OF_RANGE;
    }
    const std::string name = read_name(config->device_name);
    if (const auto valid = protocol::check_string(name); !valid) {
        return to_result(valid.error());
    }
    auto state = stored<group::GroupState>(config->group_state, config->group_state_size,
                                           group::decode_state);
    if (!state) {
        return to_result(state.error());
    }
    auto arrangement = stored<layout::LayoutState>(config->layout_state, config->layout_state_size,
                                                   layout::decode_layout);
    if (!arrangement) {
        return to_result(arrangement.error());
    }
    mesh::MeshConfig mesh_config;
    mesh_config.platform = static_cast<protocol::Platform>(config->platform);
    mesh_config.device_name = name;
    mesh_config.group_id = state.value().group_id;
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
    // mesh → group → layout → callbacks
    handle->layout = std::make_unique<layout::Layout>(
        osdrop::crypto::device_id(identity.value().public_key()), std::move(arrangement.value()),
        handle->observer, handle->observer);
    handle->observer.follow(*handle->layout);
    handle->layout->group_changed(state.value().group_id, members_of(state.value()));
    handle->group = std::make_unique<group::Group>(
        group::GroupConfig{.platform = mesh_config.platform, .device_name = name}, identity.value(),
        std::move(state.value()), handle->random, handle->observer, *handle->layout);
    group::Group& member_layer = *handle->group;
    layout::Layout& arrangement_layer = *handle->layout;
    auto node = osdrop::transport::AsioNode::create(
        std::move(mesh_config), std::move(identity.value()), handle->random, member_layer,
        osdrop::transport::NodeOptions{.listen_port = config->listen_port},
        [&member_layer, &arrangement_layer](mesh::Mesh& mesh) {
            member_layer.attach(mesh);
            arrangement_layer.attach(mesh);
        });
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

osdrop_result osdrop_mesh_group_id(osdrop_mesh* mesh, uint8_t* out_group_id,
                                   uint8_t* out_has_group) {
    if (mesh == nullptr || out_group_id == nullptr || out_has_group == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    protocol::Id group_id{};
    const group::Group& member_layer = *mesh->group;
    if (!mesh->node->run_and_wait([&](mesh::Mesh& /*mesh*/, TimePoint /*now*/) {
            group_id = member_layer.state().group_id;
        })) {
        return OSDROP_ERROR_INTERNAL;
    }
    std::ranges::copy(group_id, out_group_id);
    *out_has_group = static_cast<std::uint8_t>(!protocol::is_zero(group_id));
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
    return to_result(mesh->node->confirm_pairing(connection, accepted == 1));
}

osdrop_result osdrop_mesh_members(osdrop_mesh* mesh, osdrop_member* out, size_t capacity,
                                  size_t* out_count) {
    if (mesh == nullptr || (out == nullptr && capacity != 0) || out_count == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    std::vector<group::MemberStatus> members;
    const group::Group& member_layer = *mesh->group;
    if (!mesh->node->run_and_wait(
            [&](mesh::Mesh& /*mesh*/, TimePoint /*now*/) { members = member_layer.members(); })) {
        return OSDROP_ERROR_INTERNAL;
    }
    *out_count = members.size();
    if (members.size() > capacity) {
        return OSDROP_ERROR_BUFFER_TOO_SMALL;
    }
    std::ranges::transform(members, out,
                           [](const group::MemberStatus& status) { return describe(status); });
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_revoke(osdrop_mesh* mesh, const uint8_t* device_id) {
    if (mesh == nullptr || device_id == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const protocol::Id device = read_id(device_id);
    auto result = osdrop::Status<Error>::failure(Error::Internal);
    group::Group& member_layer = *mesh->group;
    static_cast<void>(mesh->node->run_and_wait(
        [&](mesh::Mesh& /*mesh*/, TimePoint now) { result = member_layer.revoke(device, now); }));
    return to_result(result);
}

osdrop_result osdrop_mesh_leave_group(osdrop_mesh* mesh) {
    if (mesh == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    auto result = osdrop::Status<Error>::failure(Error::Internal);
    group::Group& member_layer = *mesh->group;
    static_cast<void>(mesh->node->run_and_wait(
        [&](mesh::Mesh& /*mesh*/, TimePoint now) { result = member_layer.leave(now); }));
    return to_result(result);
}

osdrop_result osdrop_mesh_set_permissions(osdrop_mesh* mesh, const uint8_t* device_id,
                                          const osdrop_permissions* permissions) {
    if (mesh == nullptr || device_id == nullptr || permissions == nullptr ||
        permissions->control > 1 || permissions->files > 1 || permissions->clipboard > 1) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const protocol::Id device = read_id(device_id);
    const group::Permissions wanted{.control = permissions->control == 1,
                                    .files = permissions->files == 1,
                                    .clipboard = permissions->clipboard == 1};
    auto result = osdrop::Status<Error>::failure(Error::Internal);
    group::Group& member_layer = *mesh->group;
    static_cast<void>(mesh->node->run_and_wait([&](mesh::Mesh& /*mesh*/, TimePoint now) {
        result = member_layer.set_permissions(device, wanted, now);
    }));
    return to_result(result);
}

osdrop_result osdrop_mesh_set_screens(osdrop_mesh* mesh, const osdrop_monitor* monitors,
                                      size_t count) {
    if (mesh == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    auto screens = to_screens(monitors, count);
    if (!screens) {
        return to_result(screens.error());
    }
    auto result = osdrop::Status<Error>::failure(Error::Internal);
    layout::Layout& arrangement = *mesh->layout;
    static_cast<void>(mesh->node->run_and_wait([&](mesh::Mesh& /*mesh*/, TimePoint now) {
        result = arrangement.set_screens(std::move(screens.value()), now);
    }));
    return to_result(result);
}

osdrop_result osdrop_mesh_layout(osdrop_mesh* mesh, osdrop_layout_computer* out, size_t capacity,
                                 size_t* out_count) {
    if (mesh == nullptr || (out == nullptr && capacity != 0) || out_count == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    layout::LayoutState state;
    const layout::Layout& arrangement = *mesh->layout;
    if (!mesh->node->run_and_wait(
            [&](mesh::Mesh& /*mesh*/, TimePoint /*now*/) { state = arrangement.state(); })) {
        return OSDROP_ERROR_INTERNAL;
    }
    *out_count = state.screens.size();
    if (state.screens.size() > capacity) {
        return OSDROP_ERROR_BUFFER_TOO_SMALL;
    }
    std::ranges::transform(state.screens, out, [&](const auto& entry) {
        return describe(entry.first, entry.second, state.document.placements);
    });
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_set_layout(osdrop_mesh* mesh, const osdrop_placement* placements,
                                     size_t count) {
    if (mesh == nullptr || placements == nullptr || count == 0) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    auto wanted = to_placements(placements, count);
    if (!wanted) {
        return to_result(wanted.error());
    }
    auto result = osdrop::Status<Error>::failure(Error::Internal);
    layout::Layout& arrangement = *mesh->layout;
    static_cast<void>(mesh->node->run_and_wait([&](mesh::Mesh& /*mesh*/, TimePoint now) {
        result = arrangement.set_placements(std::move(wanted.value()), now);
    }));
    return to_result(result);
}

osdrop_result osdrop_layout_check(const osdrop_layout_computer* computers, size_t count,
                                  osdrop_layout_problem* out, size_t capacity, size_t* out_count) {
    if ((computers == nullptr && count != 0) || (out == nullptr && capacity != 0) ||
        out_count == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    layout::Placements placements;
    layout::ScreenMap screens;
    for (const osdrop_layout_computer& computer : std::span(computers, count)) {
        auto monitors = to_screens(computer.monitors, computer.monitor_count);
        if (!monitors) {
            return to_result(monitors.error());
        }
        const layout::DeviceId device = read_id(computer.placement.device_id);
        if (computer.placed > 1 || computer.placement.detached > 1 ||
            !screens.emplace(device, std::move(monitors.value())).second) {
            return OSDROP_ERROR_INVALID_ARGUMENT;
        }
        if (computer.placed == 1) {
            placements[device] = {.offset = {.x = computer.placement.x, .y = computer.placement.y},
                                  .detached = computer.placement.detached == 1};
        }
    }
    const std::vector<layout::Problem> problems = layout::check(placements, screens);
    *out_count = problems.size();
    const std::span<osdrop_layout_problem> output(out, out == nullptr ? 0 : capacity);
    for (std::size_t i = 0; i < problems.size() && i < output.size(); ++i) {
        output[i] =
            osdrop_layout_problem{.kind = static_cast<osdrop_layout_problem_kind>(problems[i].kind),
                                  .device_id = {},
                                  .other_device_id = {}};
        std::ranges::copy(problems[i].device, std::begin(output[i].device_id));
        std::ranges::copy(problems[i].other, std::begin(output[i].other_device_id));
    }
    return problems.size() > capacity ? OSDROP_ERROR_BUFFER_TOO_SMALL : OSDROP_OK;
}

osdrop_result osdrop_layout_derive_frames(osdrop_monitor* monitors, size_t count) {
    auto screens = to_screens(monitors, count);
    if (!screens) {
        return to_result(screens.error());
    }
    const std::vector<layout::Monitor> derived = layout::derive_frames(screens.value().monitors());
    if (const auto valid = protocol::encode_control(layout::Screens(derived).to_report()); !valid) {
        return to_result(valid.error());
    }
    const std::span<osdrop_monitor> output(monitors, count);
    for (std::size_t i = 0; i < derived.size(); ++i) {
        output[i].frame = to_c(derived[i].frame);
    }
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
