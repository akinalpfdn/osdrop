// The mesh through the C API, as the platforms use it: two meshes on loopback connect as members,
// pair as strangers, and report their state through C callbacks.
#include <osdrop/osdrop.h>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace {

using std::chrono::milliseconds;

constexpr std::array<std::uint8_t, OSDROP_GROUP_ID_SIZE> kGroup{7, 1, 2,  3,  4,  5,  6,  7,
                                                                8, 9, 10, 11, 12, 13, 14, 15};

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
    osdrop_trust trust = OSDROP_TRUST_TRUSTED;
    int connected = 0;
    std::vector<osdrop_result> disconnects;
    std::vector<std::pair<uint64_t, uint32_t>> codes;
    int paired = 0;
    std::vector<osdrop_result> pairing_failures;
    std::string last_peer_name;
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
    callbacks.trust = [](void* context, const osdrop_peer* /*peer*/) {
        auto* e = static_cast<Events*>(context);
        std::lock_guard lock(e->mutex);
        return e->trust;
    };
    callbacks.pairing_code = [](void* context, uint64_t connection, uint32_t code,
                                const osdrop_peer* /*peer*/) {
        auto* e = static_cast<Events*>(context);
        e->update([&] { e->codes.emplace_back(connection, code); });
    };
    callbacks.paired = [](void* context, const osdrop_peer* /*peer*/) {
        auto* e = static_cast<Events*>(context);
        e->update([&] { ++e->paired; });
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

struct CMesh {
    CMesh(std::uint64_t seed, const char* name, const std::uint8_t* group) : random{seed} {
        const osdrop_random_source source{.fill = fill, .context = &random};
        std::array<std::uint8_t, OSDROP_KEY_SIZE> public_key{};
        EXPECT_EQ(osdrop_identity_generate(&source, secret.data(), public_key.data()), OSDROP_OK);
        const osdrop_mesh_config config{.secret_key = secret.data(),
                                        .device_name = name,
                                        .platform = OSDROP_PLATFORM_MACOS,
                                        .group_id = group,
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

    Random random;
    Events events;
    std::array<std::uint8_t, OSDROP_KEY_SIZE> secret{};
    osdrop_mesh* mesh = nullptr;
    std::uint16_t port = 0;
    std::array<std::uint8_t, OSDROP_DEVICE_ID_SIZE> device_id{};
};

TEST(CapiMesh, MembersOfOneGroupConnectAndDisconnect) {
    CMesh mac(1, "Mac", kGroup.data());
    CMesh pc(2, "PC", kGroup.data());
    ASSERT_NE(mac.port, 0);
    ASSERT_EQ(osdrop_mesh_peer_discovered(mac.mesh, pc.device_id.data(), kGroup.data(), "127.0.0.1",
                                          pc.port),
              OSDROP_OK);
    ASSERT_EQ(osdrop_mesh_peer_discovered(pc.mesh, mac.device_id.data(), kGroup.data(), "127.0.0.1",
                                          mac.port),
              OSDROP_OK);
    ASSERT_TRUE(mac.events.wait_for([&] { return mac.events.connected == 1; }));
    ASSERT_TRUE(pc.events.wait_for([&] { return pc.events.connected == 1; }));
    {
        std::lock_guard lock(mac.events.mutex);
        EXPECT_EQ(mac.events.last_peer_name, "PC");
        EXPECT_GT(mac.events.log_lines, 0);
    }

    ASSERT_EQ(osdrop_mesh_close_all(pc.mesh, 1), OSDROP_OK);
    ASSERT_TRUE(mac.events.wait_for([&] { return mac.events.connected == 0; }));
    std::lock_guard lock(mac.events.mutex);
    EXPECT_EQ(mac.events.disconnects, std::vector<osdrop_result>{OSDROP_ERROR_PEER_SLEEPING});
}

TEST(CapiMesh, StrangersPairWithTheSameCode) {
    CMesh mac(3, "Mac", kGroup.data());
    CMesh pc(4, "New PC", nullptr);
    mac.events.trust = OSDROP_TRUST_UNKNOWN;
    pc.events.trust = OSDROP_TRUST_UNKNOWN;
    uint64_t connection = 0;
    ASSERT_EQ(osdrop_mesh_pair(pc.mesh, "127.0.0.1", mac.port, &connection), OSDROP_OK);
    ASSERT_TRUE(mac.events.wait_for([&] { return mac.events.codes.size() == 1; }));
    ASSERT_TRUE(pc.events.wait_for([&] { return pc.events.codes.size() == 1; }));
    uint64_t mac_connection = 0;
    {
        std::scoped_lock lock(mac.events.mutex, pc.events.mutex);
        EXPECT_EQ(mac.events.codes.front().second, pc.events.codes.front().second);
        EXPECT_EQ(pc.events.codes.front().first, connection);
        mac_connection = mac.events.codes.front().first;
    }
    ASSERT_EQ(osdrop_mesh_confirm_pairing(pc.mesh, connection, 1), OSDROP_OK);
    ASSERT_EQ(osdrop_mesh_confirm_pairing(mac.mesh, mac_connection, 1), OSDROP_OK);
    ASSERT_TRUE(mac.events.wait_for([&] { return mac.events.connected == 1; }));
    ASSERT_TRUE(pc.events.wait_for([&] { return pc.events.connected == 1; }));
    std::scoped_lock lock(mac.events.mutex, pc.events.mutex);
    EXPECT_EQ(mac.events.paired, 1);
    EXPECT_EQ(pc.events.paired, 1);
}

TEST(CapiMesh, ARejectedPairingIsReported) {
    CMesh mac(5, "Mac", kGroup.data());
    CMesh pc(6, "New PC", nullptr);
    mac.events.trust = OSDROP_TRUST_UNKNOWN;
    pc.events.trust = OSDROP_TRUST_UNKNOWN;
    uint64_t connection = 0;
    ASSERT_EQ(osdrop_mesh_pair(pc.mesh, "127.0.0.1", mac.port, &connection), OSDROP_OK);
    ASSERT_TRUE(mac.events.wait_for([&] { return mac.events.codes.size() == 1; }));
    const uint64_t mac_connection = [&] {
        std::lock_guard lock(mac.events.mutex);
        return mac.events.codes.front().first;
    }();
    ASSERT_EQ(osdrop_mesh_confirm_pairing(mac.mesh, mac_connection, 0), OSDROP_OK);
    ASSERT_TRUE(pc.events.wait_for([&] { return !pc.events.pairing_failures.empty(); }));
    std::lock_guard lock(pc.events.mutex);
    EXPECT_EQ(pc.events.pairing_failures.front(), OSDROP_ERROR_PAIRING_REJECTED);
}

TEST(CapiMesh, InvalidConfigurationsAreRefused) {
    Random random{9};
    const osdrop_random_source source{.fill = fill, .context = &random};
    std::array<std::uint8_t, OSDROP_KEY_SIZE> secret{};
    std::array<std::uint8_t, OSDROP_KEY_SIZE> public_key{};
    ASSERT_EQ(osdrop_identity_generate(&source, secret.data(), public_key.data()), OSDROP_OK);
    Events events;
    const osdrop_mesh_callbacks callbacks = callbacks_for(events);
    const osdrop_mesh_config good{.secret_key = secret.data(),
                                  .device_name = "Mac",
                                  .platform = OSDROP_PLATFORM_MACOS,
                                  .group_id = nullptr,
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
    EXPECT_EQ(osdrop_mesh_create(&good, &source, nullptr, &mesh), OSDROP_ERROR_INVALID_ARGUMENT);
    osdrop_mesh_callbacks no_trust = callbacks;
    no_trust.trust = nullptr;
    EXPECT_EQ(osdrop_mesh_create(&good, &source, &no_trust, &mesh), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(mesh, nullptr);
    EXPECT_EQ(osdrop_mesh_listen_port(nullptr, nullptr), OSDROP_ERROR_INVALID_ARGUMENT);
    osdrop_mesh_destroy(nullptr);
}

} // namespace
