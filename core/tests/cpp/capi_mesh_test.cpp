// The mesh and the group through the C API, as the platforms use them: computers on loopback pair
// into a group, store the state core hands them, reconnect from it, revoke, leave and set
// permissions, and send each other files, reporting everything through C callbacks.
#include "temp_folder.hpp"
#include "transfer/files.hpp"

#include <osdrop/osdrop.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
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
    Bytes layout; ///< the last layout state handed over
    int log_lines = 0;
    std::vector<std::pair<osdrop_result, std::uint64_t>> offers; ///< result, total bytes
    std::vector<osdrop_transfer_progress> progress;
    struct Finished {
        std::uint64_t id = 0;
        osdrop_result result = OSDROP_OK;
        std::vector<std::string> items;
        std::uint64_t skipped_links = 0;
    };
    std::vector<Finished> finished;
    struct Clip {
        DeviceId device{};
        std::uint64_t clip_id = 0;
        std::vector<osdrop_announced_format> formats;
    };
    std::vector<Clip> clips;
    std::vector<std::pair<osdrop_result, Bytes>> contents;
    std::vector<std::vector<osdrop_input>> captures; ///< keys to release here, per capture
    struct Ended {
        std::uint8_t reason = 0;
        std::uint8_t refused = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
    };
    std::vector<Ended> ended;
    std::vector<std::optional<DeviceId>> controlled;
    std::vector<osdrop_input> injected;

    bool wait_for(const std::function<bool()>& condition) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, milliseconds(5'000), condition);
    }
    /// What `read` returns, copied under the lock while callbacks may still arrive.
    template <typename Read>
    auto read(Read&& read) {
        const std::lock_guard lock(mutex);
        return read();
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
    callbacks.layout_changed = [](void* context, const uint8_t* state, size_t size) {
        auto* e = static_cast<Events*>(context);
        e->update([&] { e->layout.assign(state, state + size); });
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
    callbacks.offer_ready = [](void* context, const osdrop_object_ref* /*item*/,
                               osdrop_result result, uint64_t total_bytes, uint64_t /*entries*/,
                               uint64_t /*skipped*/) {
        auto* e = static_cast<Events*>(context);
        e->update([&] { e->offers.emplace_back(result, total_bytes); });
    };
    callbacks.clipboard_changed = [](void* context, const uint8_t* device, uint64_t clip_id,
                                     const osdrop_announced_format* formats, size_t count) {
        auto* e = static_cast<Events*>(context);
        e->update([&] {
            Events::Clip clip{.device = {}, .clip_id = clip_id, .formats = {}};
            std::memcpy(clip.device.data(), device, clip.device.size());
            const auto announced = std::span(formats, count);
            clip.formats.assign(announced.begin(), announced.end());
            e->clips.push_back(std::move(clip));
        });
    };
    callbacks.content_received = [](void* context, uint64_t /*transfer_id*/, osdrop_result result,
                                    const uint8_t* data, size_t size) {
        auto* e = static_cast<Events*>(context);
        e->update([&] {
            const auto content = std::span(data, size);
            e->contents.emplace_back(result, Bytes(content.begin(), content.end()));
        });
    };
    callbacks.capture_began = [](void* context, const uint8_t* /*device*/, const osdrop_input* keys,
                                 size_t count) {
        auto* e = static_cast<Events*>(context);
        e->update([&] {
            const auto released = std::span(keys, count);
            e->captures.emplace_back(released.begin(), released.end());
        });
    };
    callbacks.capture_ended = [](void* context, const uint8_t* /*device*/, int32_t x, int32_t y,
                                 uint8_t reason, uint8_t refused, uint8_t /*caps_lock*/) {
        auto* e = static_cast<Events*>(context);
        e->update(
            [&] { e->ended.push_back({.reason = reason, .refused = refused, .x = x, .y = y}); });
    };
    callbacks.controlled = [](void* context, const uint8_t* controller) {
        auto* e = static_cast<Events*>(context);
        e->update([&] {
            std::optional<DeviceId> device;
            if (controller != nullptr) {
                device.emplace();
                std::memcpy(device->data(), controller, device->size());
            }
            e->controlled.push_back(device);
        });
    };
    callbacks.inject = [](void* context, const osdrop_input* input) {
        auto* e = static_cast<Events*>(context);
        e->update([&] { e->injected.push_back(*input); });
    };
    callbacks.transfer_progress = [](void* context, const osdrop_transfer_progress* progress) {
        auto* e = static_cast<Events*>(context);
        e->update([&] { e->progress.push_back(*progress); });
    };
    callbacks.transfer_finished = [](void* context, uint64_t transfer_id, osdrop_result result,
                                     const char* const* items, size_t item_count,
                                     uint64_t skipped_links) {
        auto* e = static_cast<Events*>(context);
        e->update([&] {
            Events::Finished finished{
                .id = transfer_id, .result = result, .items = {}, .skipped_links = skipped_links};
            for (const char* item : std::span(items, item_count)) {
                finished.items.emplace_back(item);
            }
            e->finished.push_back(std::move(finished));
        });
    };
    return callbacks;
}

/// One computer. The same seed gives the same identity, so a computer can be started again with
/// the state it stored.
struct CMesh {
    CMesh(std::uint64_t seed, const char* name, const Bytes& state = {}, const Bytes& layout = {})
        : random{seed} {
        const osdrop_random_source source{.fill = fill, .context = &random};
        std::array<std::uint8_t, OSDROP_KEY_SIZE> public_key{};
        EXPECT_EQ(osdrop_identity_generate(&source, secret.data(), public_key.data()), OSDROP_OK);
        const osdrop_mesh_config config{.secret_key = secret.data(),
                                        .device_name = name,
                                        .platform = OSDROP_PLATFORM_MACOS,
                                        .group_state = state.empty() ? nullptr : state.data(),
                                        .group_state_size = state.size(),
                                        .layout_state = layout.empty() ? nullptr : layout.data(),
                                        .layout_state_size = layout.size(),
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
    [[nodiscard]] std::vector<osdrop_layout_computer> layout() const {
        size_t count = 0;
        const osdrop_result counted = osdrop_mesh_layout(mesh, nullptr, 0, &count);
        EXPECT_EQ(counted, count == 0 ? OSDROP_OK : OSDROP_ERROR_BUFFER_TOO_SMALL);
        std::vector<osdrop_layout_computer> list(count);
        EXPECT_EQ(osdrop_mesh_layout(mesh, list.data(), list.size(), &count), OSDROP_OK);
        list.resize(count);
        return list;
    }
    [[nodiscard]] Bytes stored_layout() {
        std::lock_guard lock(events.mutex);
        return events.layout;
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

osdrop_monitor monitor(std::uint8_t key, osdrop_rect frame, osdrop_rect native, uint8_t primary) {
    return {.key = {key},
            .frame = frame,
            .native = native,
            .pixel_width = static_cast<uint16_t>(native.width / 16),
            .pixel_height = static_cast<uint16_t>(native.height / 16),
            .primary = primary};
}

osdrop_rect points(int32_t x, int32_t y, int32_t width, int32_t height) {
    return {.x = x * 16, .y = y * 16, .width = width * 16, .height = height * 16};
}

bool placed_side_by_side(const CMesh& computer) {
    const auto computers = computer.layout();
    return computers.size() == 2 &&
           std::ranges::all_of(computers,
                               [](const osdrop_layout_computer& c) { return c.placed == 1; });
}

TEST(CapiMesh, MembersShareTheirScreensAndOneArrangementThatIsStored) {
    Bytes mac_state;
    Bytes mac_layout;
    {
        CMesh mac(14, "Mac");
        CMesh pc(15, "PC");
        pair(pc, mac);
        ASSERT_TRUE(grouped(mac, pc));
        const osdrop_monitor built_in =
            monitor(1, points(0, 0, 1470, 956), points(0, 0, 1470, 956), 1);
        ASSERT_EQ(osdrop_mesh_set_screens(mac.mesh, &built_in, 1), OSDROP_OK);
        // A 150% panel: 1280 x 800 points, 1920 x 1200 pixels.
        const osdrop_monitor panel =
            monitor(2, points(0, 0, 1280, 800), points(0, 0, 1920, 1200), 1);
        ASSERT_EQ(osdrop_mesh_set_screens(pc.mesh, &panel, 1), OSDROP_OK);
        ASSERT_TRUE(
            wait_until([&] { return placed_side_by_side(mac) && placed_side_by_side(pc); }));

        const auto computers = mac.layout();
        for (const osdrop_layout_computer& c : computers) {
            EXPECT_EQ(c.monitor_count, 1);
            EXPECT_EQ(c.placement.detached, 0);
        }
        std::vector<osdrop_placement> placements;
        for (const osdrop_layout_computer& c : computers) {
            placements.push_back(c.placement);
        }
        // Overlapping them is refused; detaching one is fine.
        std::vector<osdrop_placement> overlapping = placements;
        overlapping[1].x = overlapping[0].x;
        overlapping[1].y = overlapping[0].y;
        EXPECT_EQ(osdrop_mesh_set_layout(mac.mesh, overlapping.data(), overlapping.size()),
                  OSDROP_ERROR_INVALID_LAYOUT);
        placements[1].detached = 1;
        ASSERT_EQ(osdrop_mesh_set_layout(mac.mesh, placements.data(), placements.size()),
                  OSDROP_OK);
        ASSERT_TRUE(wait_until([&] {
            const auto seen = pc.layout();
            return seen.size() == 2 && seen[1].placement.detached == 1;
        }));
        mac_state = mac.stored_state();
        mac_layout = mac.stored_layout();
    }
    ASSERT_FALSE(mac_layout.empty());
    CMesh mac(14, "Mac", mac_state, mac_layout);
    const auto restored = mac.layout();
    ASSERT_EQ(restored.size(), 2U) << "both computers, the PC offline";
    EXPECT_EQ(restored[1].placement.detached, 1);
}

TEST(CapiMesh, LayoutChecksAndWindowsFramesArePure) {
    // A 150% laptop panel with a 100% monitor on its right, 300 pixels lower.
    std::array<osdrop_monitor, 2> windows{
        monitor(1, points(0, 0, 1280, 800), points(0, 0, 1920, 1200), 1),
        monitor(2, points(0, 0, 2560, 1440), points(1920, 300, 2560, 1440), 0)};
    ASSERT_EQ(osdrop_layout_derive_frames(windows.data(), windows.size()), OSDROP_OK);
    EXPECT_EQ(windows[1].frame.x, 1280 * 16);
    EXPECT_EQ(windows[1].frame.y, 200 * 16) << "300 laptop pixels are 200 points";

    std::array<osdrop_layout_computer, 2> computers{};
    computers[0] = {.placed = 1,
                    .placement = {.device_id = {1}, .x = 0, .y = 0, .detached = 0},
                    .monitor_count = 1,
                    .monitors = {monitor(1, points(0, 0, 1470, 956), points(0, 0, 1470, 956), 1)}};
    computers[1] = {.placed = 1,
                    .placement = {.device_id = {2}, .x = 1000 * 16, .y = 0, .detached = 0},
                    .monitor_count = 2,
                    .monitors = {windows[0], windows[1]}};
    std::array<osdrop_layout_problem, 4> problems{};
    size_t count = 0;
    ASSERT_EQ(osdrop_layout_check(computers.data(), computers.size(), problems.data(),
                                  problems.size(), &count),
              OSDROP_OK);
    ASSERT_EQ(count, 1U);
    EXPECT_EQ(problems[0].kind, OSDROP_LAYOUT_OVERLAP);
    computers[1].placement.x = 1470 * 16;
    ASSERT_EQ(osdrop_layout_check(computers.data(), computers.size(), problems.data(),
                                  problems.size(), &count),
              OSDROP_OK);
    EXPECT_EQ(count, 0U);

    std::array<osdrop_monitor, 2> no_primary{windows[0], windows[1]};
    no_primary[0].primary = 0;
    EXPECT_EQ(osdrop_layout_derive_frames(no_primary.data(), no_primary.size()),
              OSDROP_ERROR_VALUE_OUT_OF_RANGE);
    EXPECT_EQ(osdrop_layout_derive_frames(nullptr, 1), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_layout_check(nullptr, 1, nullptr, 0, &count), OSDROP_ERROR_INVALID_ARGUMENT);
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
                                  .layout_state = nullptr,
                                  .layout_state_size = 0,
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
    config = good;
    const Bytes newer_layout{'O', 'D', 'L', 'Y', 2};
    config.layout_state = newer_layout.data();
    config.layout_state_size = newer_layout.size();
    EXPECT_EQ(create(config), OSDROP_ERROR_UNSUPPORTED_VERSION);
    config.layout_state = nullptr;
    EXPECT_EQ(create(config), OSDROP_ERROR_INVALID_ARGUMENT) << "a size without bytes";
    EXPECT_EQ(osdrop_mesh_create(&good, &source, nullptr, &mesh), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(mesh, nullptr);
    EXPECT_EQ(osdrop_mesh_listen_port(nullptr, nullptr), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_leave_group(nullptr), OSDROP_ERROR_INVALID_ARGUMENT);
    osdrop_mesh_destroy(nullptr);
}

/// Two members on loopback, connected.
void connect_pair(CMesh& mac, CMesh& pc) {
    pair(pc, mac);
    ASSERT_TRUE(grouped(mac, pc));
    ASSERT_TRUE(mac.events.wait_for([&] { return mac.events.connected == 1; }));
    ASSERT_TRUE(pc.events.wait_for([&] { return pc.events.connected == 1; }));
}

constexpr osdrop_object_ref kDragItem{.kind = OSDROP_OBJECT_DRAG, .object_id = 1, .item_index = 0};

/// `from` offers `root` to `to`, which fetches it into `folder`; the transfer's ID.
std::uint64_t send_item(CMesh& from, CMesh& to, const std::filesystem::path& root,
                        const std::filesystem::path& folder) {
    const std::string path = osdrop::transfer::utf8_of(root);
    const std::array<const char*, 1> paths{path.c_str()};
    EXPECT_EQ(osdrop_mesh_offer(from.mesh, &kDragItem, paths.data(), paths.size(),
                                to.device_id.data(), 1),
              OSDROP_OK);
    EXPECT_TRUE(from.events.wait_for([&] { return !from.events.offers.empty(); }));
    const std::string destination = osdrop::transfer::utf8_of(folder);
    std::uint64_t id = 0;
    EXPECT_EQ(osdrop_mesh_receive(to.mesh, from.device_id.data(), &kDragItem, destination.c_str(),
                                  UINT64_MAX, &id),
              OSDROP_OK);
    return id;
}

TEST(CapiMesh, MembersSendEachOtherFilesOverLoopback) {
    const osdrop::test::TempFolder source;
    const osdrop::test::TempFolder destination;
    osdrop::test::write_file(source / "Project/a.bin", osdrop::test::pattern(3'000'000));
    osdrop::test::write_file(source / "Project/sub/b.txt", "hello");
    CMesh mac(1, "Mac");
    CMesh pc(2, "PC");
    connect_pair(mac, pc);

    const std::uint64_t id = send_item(mac, pc, source / "Project", destination.path());
    EXPECT_NE(id, 0U);
    {
        std::lock_guard lock(mac.events.mutex);
        EXPECT_EQ(mac.events.offers.front(), std::pair(OSDROP_OK, std::uint64_t{3'000'005}));
    }
    ASSERT_TRUE(pc.events.wait_for([&] { return !pc.events.finished.empty(); }));
    std::lock_guard lock(pc.events.mutex);
    const Events::Finished& finished = pc.events.finished.front();
    EXPECT_EQ(finished.id, id);
    EXPECT_EQ(finished.result, OSDROP_OK);
    EXPECT_EQ(finished.items,
              std::vector<std::string>{osdrop::transfer::utf8_of(destination / "Project")});
    EXPECT_EQ(osdrop::test::read_file(destination / "Project/a.bin"),
              osdrop::test::pattern(3'000'000));
    EXPECT_EQ(osdrop::test::read_file(destination / "Project/sub/b.txt"),
              osdrop::test::read_file(source / "Project/sub/b.txt"));
    ASSERT_FALSE(pc.events.progress.empty());
    EXPECT_EQ(pc.events.progress.back().bytes_done, 3'000'005U);
}

TEST(CapiMesh, QuittingDuringATransferLeavesNothingBehind) {
    const osdrop::test::TempFolder source;
    const osdrop::test::TempFolder destination;
    osdrop::test::write_file(source / "big.bin", osdrop::test::pattern(64'000'000));
    CMesh mac(1, "Mac");
    auto pc = std::make_unique<CMesh>(2, "PC");
    connect_pair(mac, *pc);

    send_item(mac, *pc, source / "big.bin", destination.path());
    ASSERT_TRUE(pc->events.wait_for(
        [&] { return !pc->events.progress.empty() && pc->events.progress.back().bytes_done > 0; }));
    pc.reset(); // quit while it writes
    EXPECT_TRUE(osdrop::test::listing(destination.path()).empty());
}

TEST(CapiMesh, ACopyOnOneMemberIsPastedOnAnother) {
    CMesh mac(1, "Mac");
    CMesh pc(2, "PC");
    connect_pair(mac, pc);
    const std::string text = "copied on the Mac";
    const std::array<osdrop_clipboard_content, 1> contents{
        {{.format = OSDROP_CLIPBOARD_TEXT,
          .data = reinterpret_cast<const uint8_t*>(text.data()), // NOLINT: the string's bytes
          .size = text.size()}}};
    uint64_t clip = 0;
    ASSERT_EQ(
        osdrop_mesh_clipboard_copied(mac.mesh, contents.data(), contents.size(), nullptr, 0, &clip),
        OSDROP_OK);
    ASSERT_TRUE(pc.events.wait_for([&] { return !pc.events.clips.empty(); }));
    {
        std::lock_guard lock(pc.events.mutex);
        const Events::Clip& seen = pc.events.clips.front();
        EXPECT_EQ(seen.device, mac.device_id);
        EXPECT_EQ(seen.clip_id, clip);
        ASSERT_EQ(seen.formats.size(), 1U);
        EXPECT_EQ(seen.formats[0].format, OSDROP_CLIPBOARD_TEXT);
        EXPECT_EQ(seen.formats[0].byte_count, text.size());
    }
    const osdrop_object_ref item{
        .kind = OSDROP_OBJECT_CLIPBOARD, .object_id = clip, .item_index = OSDROP_CLIPBOARD_TEXT};
    uint64_t id = 0;
    ASSERT_EQ(osdrop_mesh_fetch_content(pc.mesh, mac.device_id.data(), &item, 1024, &id),
              OSDROP_OK);
    ASSERT_TRUE(pc.events.wait_for([&] { return !pc.events.contents.empty(); }));
    std::lock_guard lock(pc.events.mutex);
    EXPECT_EQ(pc.events.contents.front().first, OSDROP_OK);
    EXPECT_EQ(pc.events.contents.front().second, Bytes(text.begin(), text.end()));
}

TEST(CapiMesh, ClipboardCommandsCheckTheirArguments) {
    CMesh mac(1, "Mac");
    uint64_t clip = 0;
    EXPECT_EQ(osdrop_mesh_clipboard_copied(nullptr, nullptr, 0, nullptr, 0, &clip),
              OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_clipboard_copied(mac.mesh, nullptr, 0, nullptr, 0, &clip),
              OSDROP_ERROR_INVALID_ARGUMENT)
        << "an empty copy";
    const std::array<osdrop_clipboard_content, 1> files_as_bytes{
        {{.format = OSDROP_CLIPBOARD_FILES, .data = nullptr, .size = 0}}};
    EXPECT_EQ(osdrop_mesh_clipboard_copied(mac.mesh, files_as_bytes.data(), 1, nullptr, 0, &clip),
              OSDROP_ERROR_INVALID_ARGUMENT);
    const DeviceId stranger{};
    const osdrop_object_ref files{
        .kind = OSDROP_OBJECT_CLIPBOARD, .object_id = 1, .item_index = OSDROP_CLIPBOARD_FILES};
    uint64_t id = 0;
    EXPECT_EQ(osdrop_mesh_fetch_content(mac.mesh, stranger.data(), &files, 64, &id),
              OSDROP_ERROR_INVALID_ARGUMENT)
        << "files are received, not fetched";
    const osdrop_object_ref text{
        .kind = OSDROP_OBJECT_CLIPBOARD, .object_id = 1, .item_index = OSDROP_CLIPBOARD_TEXT};
    EXPECT_EQ(osdrop_mesh_fetch_content(mac.mesh, stranger.data(), &text, 64, &id),
              OSDROP_ERROR_INVALID_STATE)
        << "not connected";
}

osdrop_input pointer(int32_t x, int32_t y, int32_t dx, int32_t dy) {
    osdrop_input input{};
    input.kind = OSDROP_INPUT_POINTER;
    input.x = x * 16;
    input.y = y * 16;
    input.dx = dx * 16;
    input.dy = dy * 16;
    return input;
}

osdrop_input key(uint16_t usage, uint8_t action) {
    osdrop_input input{};
    input.kind = OSDROP_INPUT_KEY;
    input.page = 7;
    input.usage = usage;
    input.action = action;
    return input;
}

TEST(CapiMesh, PushingThroughAnEdgeMovesTheInputToAnotherMember) {
    CMesh mac(31, "Mac");
    CMesh pc(32, "PC");
    connect_pair(mac, pc);
    const osdrop_monitor screen = monitor(1, points(0, 0, 1000, 800), points(0, 0, 1000, 800), 1);
    ASSERT_EQ(osdrop_mesh_set_screens(mac.mesh, &screen, 1), OSDROP_OK);
    ASSERT_EQ(osdrop_mesh_set_screens(pc.mesh, &screen, 1), OSDROP_OK);
    ASSERT_TRUE(wait_until([&] { return placed_side_by_side(mac) && placed_side_by_side(pc); }));
    std::vector<osdrop_placement> row;
    for (const osdrop_layout_computer& c : mac.layout()) {
        osdrop_placement placement = c.placement;
        const bool is_mac = std::ranges::equal(placement.device_id, mac.device_id);
        placement.x = is_mac ? 0 : 1000 * 16;
        placement.y = 0;
        row.push_back(placement);
    }
    ASSERT_EQ(osdrop_mesh_set_layout(mac.mesh, row.data(), row.size()), OSDROP_OK);
    ASSERT_TRUE(wait_until([&] {
        const auto seen = mac.layout();
        return std::ranges::any_of(seen, [](const auto& c) { return c.placement.x == 16'000; });
    }));

    // Shift held on the Mac, then a push against its right edge for over a second.
    const osdrop_input shift = key(0xE1, 1);
    ASSERT_EQ(osdrop_mesh_input(mac.mesh, &shift, 0), OSDROP_OK);
    const auto captured = [&] {
        const std::lock_guard lock(mac.events.mutex);
        return !mac.events.captures.empty();
    };
    for (int i = 0; i < 70 && !captured(); ++i) {
        const osdrop_input push = pointer(999, 400, 4, 0);
        ASSERT_EQ(osdrop_mesh_input(mac.mesh, &push, 0), OSDROP_OK);
        std::this_thread::sleep_for(milliseconds(20));
    }
    ASSERT_TRUE(mac.events.wait_for([&] { return mac.events.captures.size() == 1; }));
    const auto released = mac.events.read([&] { return mac.events.captures.back(); });
    ASSERT_EQ(released.size(), 1U);
    EXPECT_EQ(released.front().usage, 0xE1) << "released on the Mac";
    EXPECT_EQ(released.front().action, 2);
    ASSERT_TRUE(pc.events.wait_for([&] { return pc.events.injected.size() >= 3; }));
    const auto entered = pc.events.read([&] { return pc.events.injected; });
    EXPECT_EQ(entered.front().kind, OSDROP_INPUT_POINTER);
    EXPECT_EQ(entered.front().x, 0) << "it enters at the PC's left edge";
    EXPECT_EQ(entered.front().y, 400 * 16);
    EXPECT_EQ(entered.at(1).kind, OSDROP_INPUT_CAPS_LOCK);
    EXPECT_EQ(entered.at(1).action, 2) << "Caps Lock as on the Mac: off";
    EXPECT_EQ(entered.at(2).usage, 0xE1) << "Shift is held on the PC";
    EXPECT_EQ(pc.events.read([&] { return pc.events.controlled; }),
              (std::vector<std::optional<DeviceId>>{mac.device_id}));

    const osdrop_input a = key(0x04, 1);
    ASSERT_EQ(osdrop_mesh_input(mac.mesh, &a, 0), OSDROP_OK);
    ASSERT_TRUE(pc.events.wait_for([&] {
        return std::ranges::any_of(pc.events.injected, [](const osdrop_input& input) {
            return input.kind == OSDROP_INPUT_KEY && input.usage == 0x04;
        });
    }));

    ASSERT_EQ(osdrop_mesh_return_home(mac.mesh, 0), OSDROP_OK);
    ASSERT_TRUE(mac.events.wait_for([&] { return mac.events.ended.size() == 1; }));
    const auto ended = mac.events.read([&] { return mac.events.ended.back(); });
    EXPECT_EQ(ended.reason, 2);
    EXPECT_EQ(ended.x, 999 * 16) << "the cursor comes back where it left";
    ASSERT_TRUE(pc.events.wait_for([&] { return pc.events.controlled.size() == 2; }));
    EXPECT_EQ(pc.events.read([&] { return pc.events.controlled.back(); }), std::nullopt);
    std::set<uint16_t> held;
    for (const osdrop_input& input : pc.events.read([&] { return pc.events.injected; })) {
        if (input.kind == OSDROP_INPUT_KEY) {
            input.action == 2 ? static_cast<void>(held.erase(input.usage))
                              : static_cast<void>(held.insert(input.usage));
        }
    }
    EXPECT_TRUE(held.empty()) << "nothing stays pressed on the PC";
}

TEST(CapiMesh, InputCommandsCheckTheirArguments) {
    CMesh mac(33, "Mac");
    const osdrop_input fine = pointer(1, 1, 1, 0);
    EXPECT_EQ(osdrop_mesh_input(nullptr, &fine, 0), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_input(mac.mesh, nullptr, 0), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_input(mac.mesh, &fine, 0), OSDROP_OK);
    osdrop_input bad = key(0, 1);
    EXPECT_EQ(osdrop_mesh_input(mac.mesh, &bad, 0), OSDROP_ERROR_INVALID_ARGUMENT) << "usage 0";
    bad = key(4, 4);
    EXPECT_EQ(osdrop_mesh_input(mac.mesh, &bad, 0), OSDROP_ERROR_INVALID_ARGUMENT) << "action";
    bad = key(4, 1);
    bad.page = 9;
    EXPECT_EQ(osdrop_mesh_input(mac.mesh, &bad, 0), OSDROP_ERROR_INVALID_ARGUMENT) << "page";
    bad = osdrop_input{};
    bad.kind = OSDROP_INPUT_BUTTON;
    bad.action = 1;
    bad.click_count = 0;
    EXPECT_EQ(osdrop_mesh_input(mac.mesh, &bad, 0), OSDROP_ERROR_INVALID_ARGUMENT) << "clicks";
    bad.click_count = 1;
    bad.button = 32;
    EXPECT_EQ(osdrop_mesh_input(mac.mesh, &bad, 0), OSDROP_ERROR_INVALID_ARGUMENT) << "button";
    bad = osdrop_input{};
    bad.kind = OSDROP_INPUT_SCROLL;
    bad.scroll_unit = 3;
    EXPECT_EQ(osdrop_mesh_input(mac.mesh, &bad, 0), OSDROP_ERROR_INVALID_ARGUMENT) << "unit";
    bad.scroll_unit = 1;
    bad.scroll_phase = 6;
    EXPECT_EQ(osdrop_mesh_input(mac.mesh, &bad, 0), OSDROP_ERROR_INVALID_ARGUMENT) << "phase";
    bad = osdrop_input{};
    EXPECT_EQ(osdrop_mesh_input(mac.mesh, &bad, 0), OSDROP_ERROR_INVALID_ARGUMENT) << "kind";
    EXPECT_EQ(osdrop_mesh_return_home(mac.mesh, 2), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_return_home(mac.mesh, 0), OSDROP_OK);
    EXPECT_EQ(osdrop_mesh_lock_input(mac.mesh, 2), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_lock_input(mac.mesh, 1), OSDROP_OK);
    EXPECT_EQ(osdrop_mesh_set_unavailable(mac.mesh, 4), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_set_unavailable(mac.mesh, 1), OSDROP_OK);
    osdrop_input_settings settings{
        .modifier_mapping = 1, .immediate = 0, .crossing_modifiers = 0, .push_ms = 500};
    EXPECT_EQ(osdrop_mesh_set_input_settings(mac.mesh, &settings), OSDROP_OK);
    settings.push_ms = 5'001;
    EXPECT_EQ(osdrop_mesh_set_input_settings(mac.mesh, &settings), OSDROP_ERROR_INVALID_ARGUMENT);
    settings.push_ms = 0;
    settings.modifier_mapping = 2;
    EXPECT_EQ(osdrop_mesh_set_input_settings(mac.mesh, &settings), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_set_input_settings(mac.mesh, nullptr), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_set_pointer_speed(mac.mesh, mac.device_id.data(), 1'500), OSDROP_OK);
    EXPECT_EQ(osdrop_mesh_set_pointer_speed(mac.mesh, mac.device_id.data(), 249),
              OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_set_pointer_speed(mac.mesh, nullptr, 1'000),
              OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_set_caps_lock(mac.mesh, 1), OSDROP_OK);
    EXPECT_EQ(osdrop_mesh_set_caps_lock(mac.mesh, 2), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_cursor_moved(nullptr, 0, 0), OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_cursor_moved(mac.mesh, 0, 0), OSDROP_OK);
}

TEST(CapiMesh, TransferCommandsCheckTheirArguments) {
    CMesh mac(1, "Mac");
    const osdrop::test::TempFolder temp;
    const std::string folder = osdrop::transfer::utf8_of(temp.path());
    const std::array<const char*, 1> paths{folder.c_str()};
    const DeviceId stranger{};
    std::uint64_t id = 0;
    EXPECT_EQ(osdrop_mesh_offer(nullptr, &kDragItem, paths.data(), 1, nullptr, 0),
              OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_offer(mac.mesh, &kDragItem, paths.data(), 0, nullptr, 0),
              OSDROP_ERROR_INVALID_ARGUMENT);
    const std::array<const char*, 1> relative{"relative/path"};
    EXPECT_EQ(osdrop_mesh_offer(mac.mesh, &kDragItem, relative.data(), 1, nullptr, 0),
              OSDROP_ERROR_INVALID_ARGUMENT);
    const osdrop_object_ref no_id{.kind = OSDROP_OBJECT_DRAG, .object_id = 0, .item_index = 0};
    EXPECT_EQ(osdrop_mesh_offer(mac.mesh, &no_id, paths.data(), 1, nullptr, 0),
              OSDROP_ERROR_VALUE_OUT_OF_RANGE);
    const osdrop_object_ref no_format{
        .kind = OSDROP_OBJECT_CLIPBOARD, .object_id = 1, .item_index = 6};
    EXPECT_EQ(osdrop_mesh_offer(mac.mesh, &no_format, paths.data(), 1, nullptr, 0),
              OSDROP_ERROR_VALUE_OUT_OF_RANGE);
    EXPECT_EQ(osdrop_mesh_offer(mac.mesh, &kDragItem, paths.data(), 1, nullptr, 0), OSDROP_OK);

    EXPECT_EQ(
        osdrop_mesh_receive(mac.mesh, stranger.data(), &kDragItem, "relative", UINT64_MAX, &id),
        OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(
        osdrop_mesh_receive(mac.mesh, stranger.data(), &kDragItem, folder.c_str(), UINT64_MAX, &id),
        OSDROP_ERROR_NOT_PERMITTED)
        << "not a member";
    EXPECT_EQ(osdrop_mesh_receive(mac.mesh, stranger.data(), &kDragItem, folder.c_str(), UINT64_MAX,
                                  nullptr),
              OSDROP_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osdrop_mesh_withdraw(mac.mesh, OSDROP_OBJECT_DRAG, 0),
              OSDROP_ERROR_VALUE_OUT_OF_RANGE);
    EXPECT_EQ(osdrop_mesh_withdraw(mac.mesh, OSDROP_OBJECT_DRAG, 1), OSDROP_OK);
    EXPECT_EQ(osdrop_mesh_cancel_transfer(mac.mesh, 99), OSDROP_OK);
    EXPECT_EQ(osdrop_mesh_cancel_transfer(nullptr, 99), OSDROP_ERROR_INVALID_ARGUMENT);
}

/// An environment variable; MSVC refuses std::getenv as unsafe.
std::optional<std::string> environment(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string text(value);
    std::free(value); // NOLINT(cppcoreguidelines-no-malloc): _dupenv_s allocates with malloc
    return text;
#else
    const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe): tests are serial here
    return value == nullptr ? std::nullopt : std::optional<std::string>(value);
#endif
}

/// A measurement, not a check: OSDROP_TRANSFER_BENCH_MB=1024 with a release build moves that many
/// megabytes between two meshes on loopback and prints the rate (DEVPLAN Phase 13: 90 MB/s on
/// wired gigabit between a Mac and a PC; loopback until the PC is available).
TEST(CapiMesh, TransferThroughputOnLoopback) {
    const std::optional<std::string> setting = environment("OSDROP_TRANSFER_BENCH_MB");
    if (!setting) {
        GTEST_SKIP() << "set OSDROP_TRANSFER_BENCH_MB to measure";
    }
    const std::size_t megabytes = std::stoul(*setting);
    const osdrop::test::TempFolder source;
    const osdrop::test::TempFolder destination;
    {
        std::ofstream out(osdrop::transfer::native_path(source / "bench.bin"), std::ios::binary);
        const auto block = osdrop::test::pattern(std::size_t{1} << 20U);
        for (std::size_t i = 0; i < megabytes; ++i) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): bytes as chars
            out.write(reinterpret_cast<const char*>(block.data()),
                      static_cast<std::streamsize>(block.size()));
        }
    }
    CMesh mac(1, "Mac");
    CMesh pc(2, "PC");
    connect_pair(mac, pc);
    const auto started = std::chrono::steady_clock::now();
    send_item(mac, pc, source / "bench.bin", destination.path());
    std::unique_lock lock(pc.events.mutex);
    ASSERT_TRUE(pc.events.changed.wait_for(lock, std::chrono::minutes(10),
                                           [&] { return !pc.events.finished.empty(); }));
    ASSERT_EQ(pc.events.finished.front().result, OSDROP_OK);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double rate = static_cast<double>(megabytes) * 1.048576 / seconds;
    std::cout << megabytes << " MiB in " << seconds << " s: " << rate << " MB/s\n";
    RecordProperty("megabytes_per_second", std::to_string(rate));
}

} // namespace
