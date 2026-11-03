// The mesh and the group through the C API, as the platforms use them: computers on loopback pair
// into a group, store the state core hands them, reconnect from it, and revoke, leave and set
// permissions, reporting everything through C callbacks.
#include <osdrop/osdrop.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using std::chrono::milliseconds;
using Bytes = std::vector<std::uint8_t>;
using GroupId = std::array<std::uint8_t, OSDROP_GROUP_ID_SIZE>;
using DeviceId = std::array<std::uint8_t, OSDROP_DEVICE_ID_SIZE>;

/// Deterministic per seed; the mesh only needs distinct keys and tokens here.
struct Random {
    std::uint64_t state;
};

osdrop_result fill(void* context, uint8_t* buffer, size_t length) {
    auto* random = static_cast<Random*>(context);
    for (size_t i = 0; i < length; ++i) {
        random->state = random->state * 6364136223846793005ULL + 1442695040888963407ULL;
        buffer[i] = static_cast<uint8_t>(random->state >> 33U);
    }
    return OSDROP_OK;
}

/// Collects callbacks; tests wait on it.
struct Events {
    std::mutex mutex;
    std::condition_variable changed;
    int connected = 0;
    std::vector<osdrop_result> disconnects;
    std::vector<std::pair<uint64_t, uint32_t>> codes;
    std::vector<osdrop_result> pairing_failures;
    std::string last_peer_name;
    Bytes state;                  ///< the last group state handed over
    std::optional<GroupId> group; ///< the group to advertise with it
    std::vector<osdrop_result> left;
    int log_lines = 0;

    bool wait_for(const std::function<bool()>& condition) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, milliseconds(5'000), condition);
    }
    template <typename Change>
    void update(Change&& change) {
        {
            std::lock_guard lock(mutex);
            change();
        }
        changed.notify_all();
    }
};

osdrop_mesh_callbacks callbacks_for(Events& events) {
    osdrop_mesh_callbacks callbacks{};
    callbacks.context = &events;
    callbacks.group_changed = [](void* context, const uint8_t* state, size_t size,
                                 const uint8_t* group_id) {
        auto* e = static_cast<Events*>(context);
        e->update([&] {
            e->state.assign(state, state + size);
            e->group.reset();
            if (group_id != nullptr) {
                e->group.emplace();
                std::memcpy(e->group->data(), group_id, OSDROP_GROUP_ID_SIZE);
            }
        });
    };
    callbacks.left_group = [](void* context, osdrop_result reason) {
        auto* e = static_cast<Events*>(context);
        e->update([&] { e->left.push_back(reason); });
    };
    callbacks.pairing_code = [](void* context, uint64_t connection, uint32_t code,
                                const osdrop_peer* /*peer*/) {
        auto* e = static_cast<Events*>(context);
        e->update([&] { e->codes.emplace_back(connection, code); });
    };
    callbacks.pairing_failed = [](void* context, uint64_t /*connection*/, osdrop_result reason) {
        auto* e = static_cast<Events*>(context);
        e->update([&] { e->pairing_failures.push_back(reason); });
    };
    callbacks.peer_connected = [](void* context, const osdrop_peer* peer, uint32_t /*features*/) {
        auto* e = static_cast<Events*>(context);
        e->update([&] {
            ++e->connected;
            e->last_peer_name = peer->name;
        });
    };
    callbacks.peer_disconnected = [](void* context, const uint8_t* /*device*/,
                                     osdrop_result reason) {
        auto* e = static_cast<Events*>(context);
        e->update([&] {
            --e->connected;
            e->disconnects.push_back(reason);
        });
    };
    callbacks.log = [](void* context, osdrop_log_level /*level*/, const char* message) {
        auto* e = static_cast<Events*>(context);
        e->update([&] { e->log_lines += message[0] != '\0' ? 1 : 0; });
    };
    return callbacks;
}

/// One computer. The same seed gives the same identity, so a computer can be started again with
/// the state it stored.
struct CMesh {
    CMesh(std::uint64_t seed, const char* name, const Bytes& state = {}) : random{seed} {
        const osdrop_random_source source{.fill = fill, .context = &random};
        std::array<std::uint8_t, OSDROP_KEY_SIZE> public_key{};
        EXPECT_EQ(osdrop_identity_generate(&source, secret.data(), public_key.data()), OSDROP_OK);
        const osdrop_mesh_config config{.secret_key = secret.data(),
                                        .device_name = name,
                                        .platform = OSDROP_PLATFORM_MACOS,
                                        .group_state = state.empty() ? nullptr : state.data(),
                                        .group_state_size = state.size(),
                                        .features = 0x1F,
                                        .max_transfer_streams = 4,
                                        .listen_port = 0};
        const osdrop_mesh_callbacks callbacks = callbacks_for(events);
        EXPECT_EQ(osdrop_mesh_create(&config, &source, &callbacks, &mesh), OSDROP_OK);
        EXPECT_EQ(osdrop_mesh_listen_port(mesh, &port), OSDROP_OK);
        EXPECT_EQ(osdrop_mesh_device_id(mesh, device_id.data()), OSDROP_OK);
    }
    ~CMesh() { osdrop_mesh_destroy(mesh); }
    CMesh(const CMesh&) = delete;
    CMesh& operator=(const CMesh&) = delete;
    CMesh(CMesh&&) = delete;
    CMesh& operator=(CMesh&&) = delete;

    [[nodiscard]] std::vector<osdrop_member> members() const {
        size_t count = 0;
        const osdrop_result counted = osdrop_mesh_members(mesh, nullptr, 0, &count);
        EXPECT_EQ(counted, count == 0 ? OSDROP_OK : OSDROP_ERROR_BUFFER_TOO_SMALL);
        std::vector<osdrop_member> list(count);
        EXPECT_EQ(osdrop_mesh_members(mesh, list.data(), list.size(), &count), OSDROP_OK);
        list.resize(count);
        return list;
    }
    [[nodiscard]] std::optional<GroupId> group() const {
        GroupId id{};
        uint8_t has_group = 0;
        EXPECT_EQ(osdrop_mesh_group_id(mesh, id.data(), &has_group), OSDROP_OK);
        return has_group == 1 ? std::optional(id) : std::nullopt;
    }
    [[nodiscard]] Bytes stored_state() {
        std::lock_guard lock(events.mutex);
        return events.state;
    }
    void discovered(const CMesh& other) const {
        const std::optional<GroupId> other_group = other.group();
        EXPECT_EQ(osdrop_mesh_peer_discovered(mesh, other.device_id.data(),
                                              other_group ? other_group->data() : nullptr,
                                              "127.0.0.1", other.port),
                  OSDROP_OK);
    }

    Random random;
    Events events;
    std::array<std::uint8_t, OSDROP_KEY_SIZE> secret{};
    osdrop_mesh* mesh = nullptr;
    std::uint16_t port = 0;
    DeviceId device_id{};
};

const osdrop_member* find(const std::vector<osdrop_member>& members, const DeviceId& device) {
    for (const osdrop_member& member : members) {
        if (std::memcmp(member.device_id, device.data(), device.size()) == 0) {
            return &member;
        }
    }
    return nullptr;
}

/// `joiner` pairs with `host`, and both users confirm the same code.
void pair(CMesh& joiner, CMesh& host) {
    const std::size_t host_codes = [&] {
        std::lock_guard lock(host.events.mutex);
        return host.events.codes.size();
    }();
    uint64_t connection = 0;
    ASSERT_EQ(osdrop_mesh_pair(joiner.mesh, "127.0.0.1", host.port, &connection), OSDROP_OK);
    ASSERT_TRUE(host.events.wait_for([&] { return host.events.codes.size() > host_codes; }));
    ASSERT_TRUE(joiner.events.wait_for([&] {
        return !joiner.events.codes.empty() && joiner.events.codes.back().first == connection;
    }));
    uint64_t host_connection = 0;
    {
        std::scoped_lock lock(host.events.mutex, joiner.events.mutex);
        ASSERT_EQ(host.events.codes.back().second, joiner.events.codes.back().second);
        host_connection = host.events.codes.back().first;
    }
    ASSERT_EQ(osdrop_mesh_confirm_pairing(joiner.mesh, connection, 1), OSDROP_OK);
    ASSERT_EQ(osdrop_mesh_confirm_pairing(host.mesh, host_connection, 1), OSDROP_OK);
}

bool wait_until(const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() + milliseconds(5'000);
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(milliseconds(10));
    }
    return true;
}

bool in_group_of(const CMesh& computer, std::size_t size) {
    return computer.group().has_value() && computer.members().size() == size;
}

bool grouped(const CMesh& a, const CMesh& b) {
    return wait_until([&] { return in_group_of(a, 2) && in_group_of(b, 2); });
}

TEST(CapiMesh, StrangersPairIntoANewGroup) {
    CMesh mac(1, "Mac");
    CMesh pc(2, "PC");
    EXPECT_FALSE(mac.group().has_value());
    EXPECT_TRUE(mac.members().empty());

    pair(pc, mac);
    ASSERT_TRUE(mac.events.wait_for([&] { return mac.events.connected == 1; }));
    ASSERT_TRUE(pc.events.wait_for([&] { return pc.events.connected == 1; }));
    ASSERT_TRUE(grouped(mac, pc));
    EXPECT_EQ(mac.group(), pc.group());

    const auto members = mac.members();
    const osdrop_member* self = find(members, mac.device_id);
    const osdrop_member* other = find(members, pc.device_id);
    ASSERT_NE(self, nullptr);
    ASSERT_NE(other, nullptr);
    EXPECT_EQ(self->self, 1);
    EXPECT_STREQ(self->name, "Mac");
    EXPECT_EQ(other->self, 0);
    EXPECT_EQ(other->connected, 1);
    EXPECT_STREQ(other->name, "PC");
    EXPECT_EQ(other->permissions.control, 1);
    EXPECT_EQ(other->permissions.files, 1);
    EXPECT_EQ(other->permissions.clipboard, 1);

    std::lock_guard lock(mac.events.mutex);
    EXPECT_EQ(mac.events.group, mac.group()) << "the group to advertise comes with the state";
    EXPECT_FALSE(mac.events.state.empty());
    EXPECT_EQ(mac.events.last_peer_name, "PC");
    EXPECT_GT(mac.events.log_lines, 0);
}

TEST(CapiMesh, StoredStateReconnectsWithoutPairingAndKeepsPermissions) {
    Bytes mac_state;
    Bytes pc_state;
    DeviceId pc_id{};
    {
        CMesh mac(3, "Mac");
        CMesh pc(4, "PC");
        pair(pc, mac);
        ASSERT_TRUE(grouped(mac, pc));
        pc_id = pc.device_id;
        const osdrop_permissions files_only{.control = 0, .files = 1, .clipboard = 0};
        ASSERT_EQ(osdrop_mesh_set_permissions(mac.mesh, pc_id.data(), &files_only), OSDROP_OK);
        mac_state = mac.stored_state();
        pc_state = pc.stored_state();
    }

    CMesh mac(3, "Mac", mac_state);
    CMesh pc(4, "PC", pc_state);
    ASSERT_EQ(pc.device_id, pc_id);
    ASSERT_TRUE(mac.group().has_value());
    EXPECT_EQ(mac.group(), pc.group());
    mac.discovered(pc);
    pc.discovered(mac);
    ASSERT_TRUE(mac.events.wait_for([&] { return mac.events.connected == 1; }));
    ASSERT_TRUE(pc.events.wait_for([&] { return pc.events.connected == 1; }));
    {
        std::scoped_lock lock(mac.events.mutex, pc.events.mutex);
        EXPECT_TRUE(mac.events.codes.empty()) << "members need no code";
        EXPECT_TRUE(pc.events.codes.empty());
    }
    const auto members = mac.members();
    const osdrop_member* other = find(members, pc_id);
    ASSERT_NE(other, nullptr);
    EXPECT_EQ(other->permissions.control, 0);
    EXPECT_EQ(other->permissions.files, 1);
    EXPECT_EQ(other->permissions.clipboard, 0);
}

TEST(CapiMesh, ARevokedComputerLeavesItsGroup) {
    CMesh mac(5, "Mac");
    CMesh pc(6, "PC");
    pair(pc, mac);
    ASSERT_TRUE(grouped(mac, pc));
    ASSERT_TRUE(mac.events.wait_for([&] { return mac.events.connected == 1; }));

    ASSERT_EQ(osdrop_mesh_revoke(mac.mesh, pc.device_id.data()), OSDROP_OK);
    ASSERT_TRUE(pc.events.wait_for([&] { return !pc.events.left.empty(); }));
    ASSERT_TRUE(mac.events.wait_for([&] { return mac.events.connected == 0; }));
    EXPECT_EQ(mac.members().size(), 1U);
    EXPECT_TRUE(mac.group().has_value());
    EXPECT_FALSE(pc.group().has_value());
    EXPECT_TRUE(pc.members().empty());
    std::lock_guard lock(pc.events.mutex);
    EXPECT_EQ(pc.events.left, std::vector<osdrop_result>{OSDROP_ERROR_NOT_TRUSTED});
    EXPECT_FALSE(pc.events.group.has_value()) << "stops advertising the group";
}

TEST(CapiMesh, LeavingTellsTheOtherMembers) {
    CMesh mac(7, "Mac");
    CMesh pc(8, "PC");
    pair(pc, mac);
    ASSERT_TRUE(grouped(mac, pc));

    ASSERT_EQ(osdrop_mesh_leave_group(pc.mesh), OSDROP_OK);
    ASSERT_TRUE(wait_until([&] { return mac.members().size() == 1; }));
    EXPECT_FALSE(pc.group().has_value());
    std::lock_guard lock(pc.events.mutex);
    EXPECT_EQ(pc.events.left, std::vector<osdrop_result>{OSDROP_OK});
}

TEST(CapiMesh, ARejectedPairingIsReported) {
    CMesh mac(9, "Mac");
    CMesh pc(10, "New PC");
    uint64_t connection = 0;
    ASSERT_EQ(osdrop_mesh_pair(pc.mesh, "127.0.0.1", mac.port, &connection), OSDROP_OK);
    ASSERT_TRUE(mac.events.wait_for([&] { return mac.events.codes.size() == 1; }));
    const uint64_t mac_connection = [&] {
        std::lock_guard lock(mac.events.mutex);
        return mac.events.codes.front().first;
    }();
    ASSERT_EQ(osdrop_mesh_confirm_pairing(mac.mesh, mac_connection, 0), OSDROP_OK);
    ASSERT_TRUE(pc.events.wait_for([&] { return !pc.events.pairing_failures.empty(); }));
    EXPECT_FALSE(mac.group().has_value());
    std::lock_guard lock(pc.events.mutex);
    EXPECT_EQ(pc.events.pairing_failures.front(), OSDROP_ERROR_PAIRING_REJECTED);
}

TEST(CapiMesh, GroupCommandsCheckTheirArguments) {
    CMesh mac(11, "Mac");
    const osdrop_permissions all{.control = 1, .files = 1, .clipboard = 1};
    EXPECT_EQ(osdrop_mesh_leave_group(mac.mesh), OSDROP_ERROR_INVALID_STATE);
    EXPECT_EQ(osdrop_mesh_revoke(mac.mesh, mac.device_id.data()), OSDROP_ERROR_INVALID_STATE);
    EXPECT_EQ(osdrop_mesh_set_permissions(mac.mesh, mac.device_id.data(), &all),
              OSDROP_ERROR_INVALID_STATE);

    CMesh pc(12, "PC");
    pair(pc, mac);
    ASSERT_TRUE(grouped(mac, pc));
    EXPECT_EQ(osdrop_mesh_revoke(mac.mesh, mac.device_id.data()), OSDROP_ERROR_INVALID_ARGUMENT);
    const DeviceId stranger{1, 2, 3};
    EXPECT_EQ(osdrop_mesh_revoke(mac.mesh, stranger.data()), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_set_permissions(mac.mesh, stranger.data(), &all),
              OSDROP_ERROR_INVALID_ARGUMENT);
    const osdrop_permissions invalid{.control = 2, .files = 1, .clipboard = 1};
    EXPECT_EQ(osdrop_mesh_set_permissions(mac.mesh, pc.device_id.data(), &invalid),
              OSDROP_ERROR_INVALID_ARGUMENT);
    std::array<osdrop_member, 1> one{};
    size_t count = 0;
    EXPECT_EQ(osdrop_mesh_members(mac.mesh, one.data(), one.size(), &count),
              OSDROP_ERROR_BUFFER_TOO_SMALL);
    EXPECT_EQ(count, 2U);
    EXPECT_EQ(osdrop_mesh_members(mac.mesh, nullptr, 1, &count), OSDROP_ERROR_INVALID_ARGUMENT);
}

TEST(CapiMesh, InvalidConfigurationsAreRefused) {
    Random random{13};
    const osdrop_random_source source{.fill = fill, .context = &random};
    std::array<std::uint8_t, OSDROP_KEY_SIZE> secret{};
    std::array<std::uint8_t, OSDROP_KEY_SIZE> public_key{};
    ASSERT_EQ(osdrop_identity_generate(&source, secret.data(), public_key.data()), OSDROP_OK);
    Events events;
    const osdrop_mesh_callbacks callbacks = callbacks_for(events);
    const osdrop_mesh_config good{.secret_key = secret.data(),
                                  .device_name = "Mac",
                                  .platform = OSDROP_PLATFORM_MACOS,
                                  .group_state = nullptr,
                                  .group_state_size = 0,
                                  .features = 0x1F,
                                  .max_transfer_streams = 4,
                                  .listen_port = 0};
    osdrop_mesh* mesh = nullptr;

    const auto create = [&](osdrop_mesh_config config) {
        return osdrop_mesh_create(&config, &source, &callbacks, &mesh);
    };
    const std::array<std::uint8_t, OSDROP_KEY_SIZE> zero{};
    auto config = good;
    config.secret_key = zero.data();
    EXPECT_EQ(create(config), OSDROP_ERROR_INVALID_KEY);
    config = good;
    config.device_name = "";
    EXPECT_EQ(create(config), OSDROP_ERROR_INVALID_STRING);
    const std::string long_name(65, 'x');
    config.device_name = long_name.c_str();
    EXPECT_EQ(create(config), OSDROP_ERROR_INVALID_STRING);
    config = good;
    config.features = 1U << 5U;
    EXPECT_EQ(create(config), OSDROP_ERROR_VALUE_OUT_OF_RANGE);
    config = good;
    config.max_transfer_streams = 9;
    EXPECT_EQ(create(config), OSDROP_ERROR_VALUE_OUT_OF_RANGE);
    config = good;
    config.platform = static_cast<osdrop_platform>(3);
    EXPECT_EQ(create(config), OSDROP_ERROR_VALUE_OUT_OF_RANGE);
    config = good;
    config.group_state_size = 4;
    EXPECT_EQ(create(config), OSDROP_ERROR_INVALID_ARGUMENT) << "a size without bytes";
    const Bytes foreign{'X', 'X', 'X', 'X', 1};
    config.group_state = foreign.data();
    config.group_state_size = foreign.size();
    EXPECT_EQ(create(config), OSDROP_ERROR_BAD_MAGIC);
    const Bytes newer{'O', 'D', 'G', 'S', 2};
    config.group_state = newer.data();
    config.group_state_size = newer.size();
    EXPECT_EQ(create(config), OSDROP_ERROR_UNSUPPORTED_VERSION) << "kept, never overwritten";
    EXPECT_EQ(osdrop_mesh_create(&good, &source, nullptr, &mesh), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(mesh, nullptr);
    EXPECT_EQ(osdrop_mesh_listen_port(nullptr, nullptr), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_leave_group(nullptr), OSDROP_ERROR_INVALID_ARGUMENT);
    osdrop_mesh_destroy(nullptr);
}

} // namespace
