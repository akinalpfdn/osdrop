#include <osdrop/win/group_engine.hpp>

#include <algorithm>
#include <iterator>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace osdrop::win {
namespace {

constexpr std::uint8_t kTransferStreams = 4;

template <std::size_t Size>
std::array<std::uint8_t, Size> bytes_of(const std::uint8_t* bytes) {
    std::array<std::uint8_t, Size> out{};
    std::copy_n(bytes, Size, out.begin());
    return out;
}

std::vector<std::uint8_t> port_bytes(std::uint16_t port) {
    return {static_cast<std::uint8_t>(port & 0xFFU), static_cast<std::uint8_t>(port >> 8U)};
}

std::uint16_t port_of(const std::vector<std::uint8_t>& bytes) {
    return bytes.size() == 2 ? static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8U)) : 0;
}

std::filesystem::path path_of(const char* utf8) {
    const std::string_view text(utf8);
    return {std::u8string(text.begin(), text.end())};
}

std::string utf8_of(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

constexpr int kListAttempts = 8;

/// One of core's lists, counted and then read; read again when it grew in between, because core
/// keeps working on its own thread and a computer can join after the count.
template <typename Item, typename Read>
Expected<std::vector<Item>, osdrop_result> read_list(const Read& read) {
    using Result = Expected<std::vector<Item>, osdrop_result>;
    for (int attempt = 0; attempt < kListAttempts; ++attempt) {
        std::size_t count = 0;
        const osdrop_result counted = read(nullptr, 0, &count);
        if (counted != OSDROP_OK && counted != OSDROP_ERROR_BUFFER_TOO_SMALL) {
            return Result::failure(counted);
        }
        std::vector<Item> list(count);
        const osdrop_result listed = read(list.data(), list.size(), &count);
        if (listed == OSDROP_ERROR_BUFFER_TOO_SMALL) {
            continue;
        }
        if (listed != OSDROP_OK) {
            return Result::failure(listed);
        }
        list.resize(count);
        return list;
    }
    return Result::failure(OSDROP_ERROR_BUFFER_TOO_SMALL);
}

} // namespace

Expected<std::unique_ptr<GroupEngine>, osdrop_result>
GroupEngine::create(Config config, StateStore& store, Discovery& discovery, EngineEvents& events) {
    using Result = Expected<std::unique_ptr<GroupEngine>, osdrop_result>;
    if (config.random.fill == nullptr) {
        return Result::failure(OSDROP_ERROR_INVALID_ARGUMENT);
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): private constructor, owned at once
    std::unique_ptr<GroupEngine> engine(
        new GroupEngine(std::move(config), store, discovery, events));
    if (const osdrop_result started = engine->start(); started != OSDROP_OK) {
        return Result::failure(started);
    }
    return engine;
}

GroupEngine::GroupEngine(Config config, StateStore& store, Discovery& discovery,
                         EngineEvents& events)
    : config_(std::move(config)), store_(store), discovery_(discovery), events_(events) {
    if (config_.clipboard != nullptr) {
        clipboard_ = std::make_unique<ClipboardSync>(
            *config_.clipboard,
            [this](ClipboardNotice notice, const ClipDevice& device, const std::string& name) {
                events_.clipboard_notice(notice, device, name);
            },
            [this](const std::optional<ClipboardFetch>& fetch) { events_.clipboard_fetch(fetch); },
            config_.clipboard_limits);
        if (!config_.shares_clipboard) {
            clipboard_->set_sharing(false, config_.clipboard_limits.files);
        }
    }
}

GroupEngine::~GroupEngine() {
    discovery_.stop();
    osdrop_mesh_destroy(mesh_);
}

osdrop_result GroupEngine::start() {
    auto secret = load_identity();
    if (!secret) {
        return secret.error();
    }
    auto group = store_.load(StoredItem::GroupState);
    if (!group) {
        return group.error();
    }
    // An arrangement that cannot be read is rebuilt; it is not a secret.
    auto layout = store_.load(StoredItem::LayoutState);
    const std::optional<std::vector<std::uint8_t>> arrangement =
        layout ? layout.value() : std::nullopt;
    std::uint16_t port = 0;
    if (const auto stored_port = store_.load(StoredItem::ListenPort)) {
        const std::optional<std::vector<std::uint8_t>>& bytes = stored_port.value();
        if (bytes) {
            port = port_of(*bytes);
        }
    }

    osdrop_result created = create_mesh(secret.value(), group.value(), arrangement, port);
    if (created != OSDROP_OK && created != OSDROP_ERROR_UNSUPPORTED_VERSION && arrangement) {
        events_.log(OSDROP_LOG_WARNING, "The stored arrangement could not be read; it starts over");
        created = create_mesh(secret.value(), group.value(), std::nullopt, port);
    }
    if (created != OSDROP_OK) {
        return created;
    }
    if (clipboard_) {
        clipboard_mesh_ = std::make_unique<CoreClipboardMesh>(mesh_);
        clipboard_->attach(*clipboard_mesh_);
    }
    std::uint16_t listening = 0;
    static_cast<void>(osdrop_mesh_device_id(mesh_, self_.data()));
    static_cast<void>(osdrop_mesh_listen_port(mesh_, &listening));
    GroupIdBytes id{};
    std::uint8_t has_group = 0;
    static_cast<void>(osdrop_mesh_group_id(mesh_, id.data(), &has_group));
    std::optional<GroupIdBytes> current;
    if (has_group == 1) {
        current = id;
    }
    {
        const std::scoped_lock lock(mutex_);
        port_ = listening;
        group_ = current;
    }
    if (!store_.save(StoredItem::ListenPort, port_bytes(listening))) {
        events_.log(OSDROP_LOG_WARNING, "Storing the listening port failed");
    }
    advertise(current);
    browse();
    return OSDROP_OK;
}

Expected<std::vector<std::uint8_t>, osdrop_result> GroupEngine::load_identity() {
    using Result = Expected<std::vector<std::uint8_t>, osdrop_result>;
    auto stored = store_.load(StoredItem::Identity);
    if (!stored) {
        return Result::failure(stored.error());
    }
    std::array<std::uint8_t, OSDROP_KEY_SIZE> public_key{};
    const std::optional<std::vector<std::uint8_t>>& existing = stored.value();
    if (existing) {
        std::vector<std::uint8_t> secret = *existing;
        if (secret.size() != OSDROP_KEY_SIZE ||
            osdrop_identity_public_key(secret.data(), public_key.data()) != OSDROP_OK) {
            return Result::failure(OSDROP_ERROR_INVALID_KEY);
        }
        return secret;
    }
    std::vector<std::uint8_t> secret(OSDROP_KEY_SIZE);
    if (const osdrop_result generated =
            osdrop_identity_generate(&config_.random, secret.data(), public_key.data());
        generated != OSDROP_OK) {
        return Result::failure(generated);
    }
    if (!store_.save(StoredItem::Identity, secret)) {
        return Result::failure(OSDROP_ERROR_INTERNAL);
    }
    return secret;
}

osdrop_result GroupEngine::create_mesh(const std::vector<std::uint8_t>& secret,
                                       const std::optional<std::vector<std::uint8_t>>& group,
                                       const std::optional<std::vector<std::uint8_t>>& layout,
                                       std::uint16_t port) {
    const osdrop_mesh_config config{.secret_key = secret.data(),
                                    .device_name = config_.device_name.c_str(),
                                    .platform = OSDROP_PLATFORM_WINDOWS,
                                    .group_state = group ? group->data() : nullptr,
                                    .group_state_size = group ? group->size() : 0,
                                    .layout_state = layout ? layout->data() : nullptr,
                                    .layout_state_size = layout ? layout->size() : 0,
                                    .features = config_.features,
                                    .max_transfer_streams = kTransferStreams,
                                    .listen_port = port};
    const osdrop_mesh_callbacks mesh_callbacks = callbacks();
    osdrop_mesh* created = nullptr;
    const osdrop_result result =
        osdrop_mesh_create(&config, &config_.random, &mesh_callbacks, &created);
    if (result == OSDROP_OK) {
        mesh_ = created;
    }
    return result;
}

// --- Core's callbacks: on core's thread; they never call the mesh --------------------------------

osdrop_mesh_callbacks GroupEngine::callbacks() {
    osdrop_mesh_callbacks result{};
    result.context = this;
    result.group_changed = [](void* context, const std::uint8_t* state, std::size_t size,
                              const std::uint8_t* group_id) {
        auto* engine = static_cast<GroupEngine*>(context);
        if (!engine->store_.save(StoredItem::GroupState, std::span(state, size))) {
            engine->events_.log(OSDROP_LOG_ERROR, "Storing the group failed");
        }
        std::optional<GroupIdBytes> group;
        if (group_id != nullptr) {
            group = bytes_of<OSDROP_GROUP_ID_SIZE>(group_id);
        }
        {
            const std::scoped_lock lock(engine->mutex_);
            engine->group_ = group;
        }
        engine->advertise(group);
        engine->events_.group_changed();
        engine->events_.nearby_changed();
    };
    result.left_group = [](void* context, osdrop_result reason) {
        static_cast<GroupEngine*>(context)->events_.left_group(reason);
    };
    result.layout_changed = [](void* context, const std::uint8_t* state, std::size_t size) {
        auto* engine = static_cast<GroupEngine*>(context);
        if (!engine->store_.save(StoredItem::LayoutState, std::span(state, size))) {
            engine->events_.log(OSDROP_LOG_ERROR, "Storing the arrangement failed");
        }
        engine->events_.layout_changed();
    };
    result.pairing_code = [](void* context, std::uint64_t connection, std::uint32_t code,
                             const osdrop_peer* peer) {
        static_cast<GroupEngine*>(context)->events_.pairing_code(connection, code, *peer);
    };
    result.pairing_failed = [](void* context, std::uint64_t connection, osdrop_result reason) {
        static_cast<GroupEngine*>(context)->events_.pairing_failed(connection, reason);
    };
    result.peer_connected = [](void* context, const osdrop_peer* peer, std::uint32_t features) {
        auto* engine = static_cast<GroupEngine*>(context);
        if (engine->clipboard_) {
            engine->clipboard_->peer_connected(
                bytes_of<OSDROP_DEVICE_ID_SIZE>(std::data(peer->device_id)),
                std::string(std::data(peer->name)));
        }
        engine->events_.peer_connected(*peer, features);
        engine->events_.group_changed();
        engine->events_.nearby_changed();
    };
    result.peer_disconnected = [](void* context, const std::uint8_t* device, osdrop_result reason) {
        auto* engine = static_cast<GroupEngine*>(context);
        if (engine->clipboard_) {
            engine->clipboard_->peer_disconnected(bytes_of<OSDROP_DEVICE_ID_SIZE>(device));
        }
        engine->events_.peer_disconnected(bytes_of<OSDROP_DEVICE_ID_SIZE>(device), reason);
        engine->events_.group_changed();
    };
    result.peer_responsive = [](void* context, const std::uint8_t* device,
                                std::uint8_t responsive) {
        static_cast<GroupEngine*>(context)->events_.peer_responsive(
            bytes_of<OSDROP_DEVICE_ID_SIZE>(device), responsive == 1);
    };
    result.stats = [](void* context, const std::uint8_t* device,
                      const osdrop_session_stats* stats) {
        static_cast<GroupEngine*>(context)->events_.stats(bytes_of<OSDROP_DEVICE_ID_SIZE>(device),
                                                          *stats);
    };
    result.log = [](void* context, osdrop_log_level level, const char* message) {
        static_cast<GroupEngine*>(context)->events_.log(level, message);
    };
    result.clipboard_changed = [](void* context, const std::uint8_t* device, std::uint64_t clip_id,
                                  const osdrop_announced_format* formats, std::size_t count) {
        auto* engine = static_cast<GroupEngine*>(context);
        if (engine->clipboard_) {
            const auto announced = std::span(formats, count);
            engine->clipboard_->clipboard_changed(
                bytes_of<OSDROP_DEVICE_ID_SIZE>(device), clip_id,
                std::vector<osdrop_announced_format>(announced.begin(), announced.end()));
        }
    };
    result.content_received = [](void* context, std::uint64_t id, osdrop_result outcome,
                                 const std::uint8_t* data, std::size_t size) {
        auto* engine = static_cast<GroupEngine*>(context);
        if (engine->clipboard_) {
            const auto content = std::span(data, size);
            engine->clipboard_->content_received(
                id, outcome, std::vector<std::uint8_t>(content.begin(), content.end()));
        }
    };
    result.transfer_finished = [](void* context, std::uint64_t id, osdrop_result outcome,
                                  const char* const* items, std::size_t count,
                                  std::uint64_t /*skipped_links*/) {
        auto* engine = static_cast<GroupEngine*>(context);
        std::vector<std::filesystem::path> created;
        for (const char* item : std::span(items, count)) {
            created.push_back(path_of(item));
        }
        engine->transfers_.finished(id, outcome, created); // a drag's, or nobody's
        if (engine->clipboard_) {
            std::vector<std::string> paths;
            for (const char* item : std::span(items, count)) {
                paths.emplace_back(item);
            }
            engine->clipboard_->transfer_finished(id, outcome, std::move(paths));
        }
    };
    control_callbacks(result);
    drag_callbacks(result);
    result.transfer_progress = [](void* context, const osdrop_transfer_progress* progress) {
        auto* engine = static_cast<GroupEngine*>(context);
        if (engine->clipboard_ && progress != nullptr) {
            engine->clipboard_->transfer_progress(progress->transfer_id, progress->bytes_done);
        }
    };
    return result;
}

void GroupEngine::control_callbacks(osdrop_mesh_callbacks& result) {
    result.controlled = [](void* context, const std::uint8_t* controller) {
        std::optional<DeviceIdBytes> by;
        if (controller != nullptr) {
            by = bytes_of<OSDROP_DEVICE_ID_SIZE>(controller);
        }
        static_cast<GroupEngine*>(context)->events_.controlled(by);
    };
    result.inject = [](void* context, const osdrop_input* input) {
        if (input != nullptr) {
            static_cast<GroupEngine*>(context)->events_.inject(*input);
        }
    };
    result.capture_began = [](void* context, const std::uint8_t* device,
                              const osdrop_input* release_here, std::size_t count) {
        const auto keys = std::span(release_here, release_here == nullptr ? 0 : count);
        static_cast<GroupEngine*>(context)->events_.capture_began(
            bytes_of<OSDROP_DEVICE_ID_SIZE>(device),
            std::vector<osdrop_input>(keys.begin(), keys.end()));
    };
    result.capture_ended = [](void* context, const std::uint8_t* device, std::int32_t x,
                              std::int32_t y, std::uint8_t reason, std::uint8_t refused,
                              std::uint8_t caps_lock) {
        static_cast<GroupEngine*>(context)->events_.capture_ended(
            {.device = bytes_of<OSDROP_DEVICE_ID_SIZE>(device),
             .x = x,
             .y = y,
             .reason = reason,
             .refused = refused,
             .caps_lock = caps_lock == 1});
    };
}

void GroupEngine::drag_callbacks(osdrop_mesh_callbacks& result) {
    result.drag_arrived = [](void* context, const std::uint8_t* source, std::uint32_t drag_id,
                             std::int32_t x, std::int32_t y, const osdrop_drag_item* items,
                             std::size_t count) {
        DragArrival arrival{.source = bytes_of<OSDROP_DEVICE_ID_SIZE>(source),
                            .drag_id = drag_id,
                            .x = x,
                            .y = y,
                            .items = {}};
        for (const osdrop_drag_item& item : std::span(items, items == nullptr ? 0 : count)) {
            arrival.items.push_back({.name = item.name != nullptr ? item.name : "",
                                     .folder = item.is_folder == 1,
                                     .size = item.byte_count});
        }
        static_cast<GroupEngine*>(context)->events_.drag_arrived(arrival);
    };
    result.drag_withdrawn = [](void* context, std::uint32_t drag_id) {
        static_cast<GroupEngine*>(context)->events_.drag_withdrawn(drag_id);
    };
    result.drag_ended = [](void* context, std::uint32_t drag_id, osdrop_drag_outcome outcome) {
        static_cast<GroupEngine*>(context)->events_.drag_ended(drag_id, outcome);
    };
    result.drag_wanted = [](void* context) {
        static_cast<GroupEngine*>(context)->events_.drag_wanted();
    };
    result.transfer_planned = [](void* context, std::uint64_t id,
                                 const osdrop_received_entry* entries, std::size_t count) {
        std::vector<drag::PlannedEntry> planned;
        for (const osdrop_received_entry& entry :
             std::span(entries, entries == nullptr ? 0 : count)) {
            planned.push_back({.path = entry.path,
                               .kind = entry.kind,
                               .size = entry.byte_count,
                               .modified_us = entry.modified_us});
        }
        static_cast<GroupEngine*>(context)->transfers_.planned(id, planned);
    };
    result.entry_written = [](void* context, std::uint64_t id, std::uint32_t entry,
                              const char* path, std::uint64_t bytes, std::uint8_t complete) {
        static_cast<GroupEngine*>(context)->transfers_.written(id, entry, path_of(path), bytes,
                                                               complete == 1);
    };
}

// --- Discovery -----------------------------------------------------------------------------------

void GroupEngine::advertise(const std::optional<GroupIdBytes>& group) {
    std::uint16_t port = 0;
    {
        const std::scoped_lock lock(mutex_);
        port = port_;
    }
    if (port == 0) {
        return; // still starting: start() advertises once the port is known
    }
    osdrop_advertisement advertisement = blank_advertisement();
    advertisement.protocol_version = OSDROP_PROTOCOL_VERSION;
    advertisement.protocol_minor = OSDROP_PROTOCOL_MINOR;
    std::ranges::copy(self_, std::begin(advertisement.device_id));
    if (group) {
        advertisement.has_group = 1;
        std::ranges::copy(*group, std::begin(advertisement.group_id));
    }
    const std::size_t length = std::min(config_.device_name.size(), sizeof(advertisement.name) - 1);
    std::copy_n(config_.device_name.begin(), length, std::begin(advertisement.name));
    if (!discovery_.advertise(advertisement, port)) {
        events_.log(OSDROP_LOG_WARNING, "Advertising this computer failed");
    }
}

void GroupEngine::browse() {
    const bool started =
        discovery_.browse({.found = [this](const DiscoveredComputer& computer) { found(computer); },
                           .lost = [this](const std::string& label) { lost(label); }});
    if (!started) {
        events_.log(OSDROP_LOG_ERROR, "Browsing for other computers failed");
    }
}

void GroupEngine::found(const DiscoveredComputer& computer) {
    const auto device =
        bytes_of<OSDROP_DEVICE_ID_SIZE>(std::data(computer.advertisement.device_id));
    if (device == self_) {
        return;
    }
    {
        const std::scoped_lock lock(mutex_);
        discovered_[computer.label] = computer;
    }
    if (computer.advertisement.protocol_version == OSDROP_PROTOCOL_VERSION) {
        const std::uint8_t* group = computer.advertisement.has_group == 1
                                        ? std::data(computer.advertisement.group_id)
                                        : nullptr;
        static_cast<void>(osdrop_mesh_peer_discovered(mesh_, device.data(), group,
                                                      computer.address.c_str(), computer.port));
    }
    events_.nearby_changed();
}

void GroupEngine::lost(const std::string& label) {
    std::optional<DeviceIdBytes> device;
    {
        const std::scoped_lock lock(mutex_);
        if (const auto it = discovered_.find(label); it != discovered_.end()) {
            device = bytes_of<OSDROP_DEVICE_ID_SIZE>(std::data(it->second.advertisement.device_id));
            discovered_.erase(it);
        }
    }
    if (device) {
        static_cast<void>(osdrop_mesh_peer_lost(mesh_, device->data()));
        events_.nearby_changed();
    }
}

// --- Commands ------------------------------------------------------------------------------------

EngineInfo GroupEngine::info() const {
    EngineInfo info{.device_id = self_, .group_id = {}, .has_group = false, .listen_port = 0};
    const std::scoped_lock lock(mutex_);
    info.listen_port = port_;
    if (group_) {
        info.group_id = *group_;
        info.has_group = true;
    }
    return info;
}

Expected<std::vector<osdrop_member>, osdrop_result> GroupEngine::members() const {
    return read_list<osdrop_member>(
        [this](osdrop_member* out, std::size_t capacity, std::size_t* count) {
            return osdrop_mesh_members(mesh_, out, capacity, count);
        });
}

Expected<std::vector<osdrop_blocked_device>, osdrop_result> GroupEngine::blocked() const {
    return read_list<osdrop_blocked_device>(
        [this](osdrop_blocked_device* out, std::size_t capacity, std::size_t* count) {
            return osdrop_mesh_blocked(mesh_, out, capacity, count);
        });
}

std::vector<NearbyComputer> GroupEngine::nearby() const {
    std::set<DeviceIdBytes> member_ids; // members and blocked computers are not offered
    if (const auto list = members()) {
        for (const osdrop_member& member : list.value()) {
            member_ids.insert(bytes_of<OSDROP_DEVICE_ID_SIZE>(std::data(member.device_id)));
        }
    }
    if (const auto list = blocked()) {
        for (const osdrop_blocked_device& device : list.value()) {
            member_ids.insert(bytes_of<OSDROP_DEVICE_ID_SIZE>(std::data(device.device_id)));
        }
    }
    std::vector<NearbyComputer> result;
    const std::scoped_lock lock(mutex_);
    for (const auto& [label, computer] : discovered_) {
        const osdrop_advertisement& advertisement = computer.advertisement;
        const auto device = bytes_of<OSDROP_DEVICE_ID_SIZE>(std::data(advertisement.device_id));
        if (member_ids.contains(device)) {
            continue;
        }
        const bool other_group =
            advertisement.has_group == 1 && group_ &&
            bytes_of<OSDROP_GROUP_ID_SIZE>(std::data(advertisement.group_id)) != *group_;
        result.push_back(
            {.device_id = device,
             .name = std::string(std::data(advertisement.name)),
             .platform = advertisement.platform,
             .in_another_group = other_group,
             .needs_update = advertisement.protocol_version != OSDROP_PROTOCOL_VERSION});
    }
    std::ranges::sort(result, {}, &NearbyComputer::name);
    return result;
}

Expected<std::uint64_t, osdrop_result> GroupEngine::pair(const DeviceIdBytes& device) {
    using Result = Expected<std::uint64_t, osdrop_result>;
    std::string address;
    std::uint16_t port = 0;
    {
        const std::scoped_lock lock(mutex_);
        const auto it = std::ranges::find_if(discovered_, [&](const auto& entry) {
            return bytes_of<OSDROP_DEVICE_ID_SIZE>(
                       std::data(entry.second.advertisement.device_id)) == device;
        });
        if (it == discovered_.end()) {
            return Result::failure(OSDROP_ERROR_INVALID_ARGUMENT);
        }
        address = it->second.address;
        port = it->second.port;
    }
    std::uint64_t connection = 0;
    if (const osdrop_result paired = osdrop_mesh_pair(mesh_, address.c_str(), port, &connection);
        paired != OSDROP_OK) {
        return Result::failure(paired);
    }
    return connection;
}

osdrop_result GroupEngine::confirm_pairing(std::uint64_t connection, bool accepted) {
    return osdrop_mesh_confirm_pairing(mesh_, connection, accepted ? 1 : 0);
}

osdrop_result GroupEngine::revoke(const DeviceIdBytes& device) {
    return osdrop_mesh_revoke(mesh_, device.data());
}

osdrop_result GroupEngine::block(const DeviceIdBytes& device) {
    return osdrop_mesh_block(mesh_, device.data()); // group_changed and nearby_changed follow
}

osdrop_result GroupEngine::unblock(const DeviceIdBytes& device) {
    return osdrop_mesh_unblock(mesh_, device.data());
}

osdrop_result GroupEngine::leave_group() {
    return osdrop_mesh_leave_group(mesh_);
}

void GroupEngine::physical_input(const osdrop_input& input, std::uint64_t age_us) {
    static_cast<void>(osdrop_mesh_input(mesh_, &input, age_us)); // fails only for invalid input
}

void GroupEngine::return_home(bool input_lost) {
    static_cast<void>(osdrop_mesh_return_home(mesh_, input_lost ? 1 : 0));
}

void GroupEngine::lock_input(bool locked) {
    static_cast<void>(osdrop_mesh_lock_input(mesh_, locked ? 1 : 0));
}

void GroupEngine::set_caps_lock(bool on) {
    static_cast<void>(osdrop_mesh_set_caps_lock(mesh_, on ? 1 : 0));
}

void GroupEngine::set_unavailable(std::uint8_t reason) {
    static_cast<void>(osdrop_mesh_set_unavailable(mesh_, reason));
}

void GroupEngine::cursor_moved(std::int32_t x, std::int32_t y) {
    static_cast<void>(osdrop_mesh_cursor_moved(mesh_, x, y));
}

osdrop_result GroupEngine::set_screens(std::span<const osdrop_monitor> monitors) {
    return osdrop_mesh_set_screens(mesh_, monitors.data(), monitors.size());
}

Expected<std::vector<osdrop_layout_computer>, osdrop_result> GroupEngine::layout() const {
    return read_list<osdrop_layout_computer>(
        [this](osdrop_layout_computer* out, std::size_t capacity, std::size_t* count) {
            return osdrop_mesh_layout(mesh_, out, capacity, count);
        });
}

osdrop_result GroupEngine::set_layout(std::span<const osdrop_placement> placements) {
    return osdrop_mesh_set_layout(mesh_, placements.data(), placements.size());
}

osdrop_result GroupEngine::set_permissions(const DeviceIdBytes& device,
                                           const osdrop_permissions& permissions) {
    const osdrop_result result = osdrop_mesh_set_permissions(mesh_, device.data(), &permissions);
    if (result == OSDROP_OK) {
        events_.group_changed(); // members() carries the permissions
    }
    return result;
}

osdrop_result GroupEngine::set_input_settings(const osdrop_input_settings& settings) {
    return osdrop_mesh_set_input_settings(mesh_, &settings);
}

osdrop_result GroupEngine::set_pointer_speed(const DeviceIdBytes& device, std::uint32_t permille) {
    return osdrop_mesh_set_pointer_speed(mesh_, device.data(), permille);
}

void GroupEngine::set_clipboard_sharing(bool shared, std::uint64_t files_limit) {
    if (clipboard_) {
        clipboard_->set_sharing(shared, files_limit);
    }
}

std::optional<std::uint32_t>
GroupEngine::drag_started(std::span<const std::filesystem::path> items) {
    std::vector<std::string> paths;
    std::vector<std::string> types;
    for (const std::filesystem::path& item : items) {
        paths.push_back(utf8_of(item));
        std::error_code error;
        types.push_back(drag::content_type(utf8_of(item.filename()),
                                           std::filesystem::is_directory(item, error)));
    }
    std::vector<osdrop_dragged_item> dragged;
    dragged.reserve(paths.size());
    for (std::size_t i = 0; i < paths.size(); ++i) {
        dragged.push_back({.path = paths[i].c_str(), .content_type = types[i].c_str()});
    }
    std::uint32_t drag_id = 0;
    if (osdrop_mesh_drag_started(mesh_, dragged.data(), dragged.size(), &drag_id) != OSDROP_OK) {
        return std::nullopt;
    }
    return drag_id;
}

void GroupEngine::drag_promised() {
    static_cast<void>(osdrop_mesh_drag_promised(mesh_));
}

void GroupEngine::drag_stopped() {
    static_cast<void>(osdrop_mesh_drag_stopped(mesh_));
}

void GroupEngine::drop_finished(std::uint32_t drag_id, osdrop_drag_outcome outcome) {
    static_cast<void>(osdrop_mesh_drop_finished(mesh_, drag_id, outcome));
}

std::optional<std::uint64_t> GroupEngine::receive(const drag::SourceId& source,
                                                  std::uint32_t drag_id, std::uint32_t index,
                                                  const std::filesystem::path& folder,
                                                  const std::weak_ptr<drag::TransferSink>& sink) {
    const osdrop_object_ref item{
        .kind = OSDROP_OBJECT_DRAG, .object_id = drag_id, .item_index = index};
    const std::string destination = utf8_of(folder);
    std::uint64_t id = 0;
    transfers_.starting();
    const bool started = osdrop_mesh_receive(mesh_, source.data(), &item, destination.c_str(),
                                             UINT64_MAX, &id) == OSDROP_OK;
    const std::optional<std::uint64_t> transfer =
        started ? std::optional<std::uint64_t>(id) : std::nullopt;
    transfers_.started(transfer, sink);
    return transfer;
}

void GroupEngine::cancel(std::uint64_t transfer_id) {
    static_cast<void>(osdrop_mesh_cancel_transfer(mesh_, transfer_id));
}

void GroupEngine::close_all(bool sleeping) {
    static_cast<void>(osdrop_mesh_close_all(mesh_, sleeping ? 1 : 0));
}

void GroupEngine::resume() {
    {
        const std::scoped_lock lock(mutex_);
        discovered_.clear();
    }
    events_.nearby_changed();
    browse();
}

} // namespace osdrop::win
