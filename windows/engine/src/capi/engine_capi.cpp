#include "clipboard/win_clipboard.hpp"
#include "discovery/dns_sd_discovery.hpp"
#include "input/input_bridge.hpp"
#include "screens/monitors.hpp"
#include "store/dpapi_store.hpp"

#include <osdrop/engine.h>
#include <osdrop/win/group_engine.hpp>
#include <osdrop/win/screens.hpp>

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <exception>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace win = osdrop::win;

/// osdrop_random_source.fill backed by the system's preferred CSPRNG.
osdrop_result system_random(void* /*context*/, std::uint8_t* buffer, std::size_t length) {
    if (buffer == nullptr || length > ULONG_MAX) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    return BCryptGenRandom(nullptr, buffer, static_cast<ULONG>(length),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0
               ? OSDROP_OK
               : OSDROP_ERROR_RANDOM_UNAVAILABLE;
}

/// This PC's name as other computers show it.
std::string computer_name() {
    std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + 64> name{};
    auto length = static_cast<DWORD>(name.size());
    if (GetComputerNameExW(ComputerNamePhysicalDnsHostname, name.data(), &length) == FALSE) {
        return win::device_name({});
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(length),
                                          nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(std::max(bytes, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(length), utf8.data(), bytes,
                        nullptr, nullptr);
    return win::device_name(utf8);
}

/// The name the user chose, cleaned as core takes it; this PC's own name when there is none.
std::string chosen_name(const char* name) {
    if (name == nullptr || *name == '\0') {
        return computer_name();
    }
    return win::device_name(name);
}

void copy_name(std::string_view name, std::span<char, OSDROP_TXT_VALUE_SIZE> out) {
    const std::size_t length = std::min(name.size(), out.size() - 1);
    std::copy_n(name.begin(), length, out.begin());
    out[length] = '\0';
}

osdrop_engine_event blank_event(osdrop_engine_event_kind kind) {
    return {.kind = kind,
            .reason = OSDROP_OK,
            .level = OSDROP_LOG_INFO,
            .code = 0,
            .connection = 0,
            .device_id = {},
            .responsive = 0,
            .name = {},
            .stats = {},
            .message = nullptr,
            .bytes_done = 0,
            .bytes_total = 0};
}

/// Turns engine events into the shell's C callback.
class CallbackEvents final : public win::EngineEvents {
public:
    CallbackEvents(osdrop_engine_callback callback, void* context)
        : callback_(callback), context_(context) {}

    void group_changed() override { send(blank_event(OSDROP_ENGINE_GROUP_CHANGED)); }
    void nearby_changed() override { send(blank_event(OSDROP_ENGINE_NEARBY_CHANGED)); }
    void left_group(osdrop_result reason) override {
        osdrop_engine_event event = blank_event(OSDROP_ENGINE_LEFT_GROUP);
        event.reason = reason;
        send(event);
    }
    void pairing_code(std::uint64_t connection, std::uint32_t code,
                      const osdrop_peer& peer) override {
        osdrop_engine_event event = blank_event(OSDROP_ENGINE_PAIRING_CODE);
        event.connection = connection;
        event.code = code;
        std::ranges::copy(peer.device_id, std::begin(event.device_id));
        copy_name(std::data(peer.name), event.name);
        send(event);
    }
    void pairing_failed(std::uint64_t connection, osdrop_result reason) override {
        osdrop_engine_event event = blank_event(OSDROP_ENGINE_PAIRING_FAILED);
        event.connection = connection;
        event.reason = reason;
        send(event);
    }
    void peer_connected(const osdrop_peer& peer, std::uint32_t /*features*/) override {
        osdrop_engine_event event = blank_event(OSDROP_ENGINE_PEER_CONNECTED);
        std::ranges::copy(peer.device_id, std::begin(event.device_id));
        copy_name(std::data(peer.name), event.name);
        {
            const std::scoped_lock lock(input_mutex_);
            connected_.insert(event_id(event));
            watch();
        }
        send(event);
    }
    void peer_disconnected(const win::DeviceIdBytes& device, osdrop_result reason) override {
        osdrop_engine_event event = blank_event(OSDROP_ENGINE_PEER_DISCONNECTED);
        std::ranges::copy(device, std::begin(event.device_id));
        event.reason = reason;
        {
            const std::scoped_lock lock(input_mutex_);
            connected_.erase(device);
            watch();
        }
        send(event);
    }
    void peer_responsive(const win::DeviceIdBytes& device, bool responsive) override {
        osdrop_engine_event event = blank_event(OSDROP_ENGINE_PEER_RESPONSIVE);
        std::ranges::copy(device, std::begin(event.device_id));
        event.responsive = responsive ? 1 : 0;
        send(event);
    }
    void stats(const win::DeviceIdBytes& device, const osdrop_session_stats& stats) override {
        osdrop_engine_event event = blank_event(OSDROP_ENGINE_STATS);
        std::ranges::copy(device, std::begin(event.device_id));
        event.stats = stats;
        send(event);
    }
    void log(osdrop_log_level level, const char* message) override {
        osdrop_engine_event event = blank_event(OSDROP_ENGINE_LOG);
        event.level = level;
        event.message = message;
        send(event);
    }
    void clipboard_notice(win::ClipboardNotice notice, const win::DeviceIdBytes& device,
                          const std::string& name) override {
        osdrop_engine_event event = blank_event(kind_of(notice));
        std::ranges::copy(device, std::begin(event.device_id));
        copy_name(name, event.name);
        send(event);
    }
    void layout_changed() override { send(blank_event(OSDROP_ENGINE_LAYOUT_CHANGED)); }
    void controlled(const std::optional<win::DeviceIdBytes>& controller) override {
        if (osdrop::engine::InputBridge* bridge = input_.load()) {
            bridge->controlled(controller.has_value());
        }
        change_input([&](osdrop_engine_input_state& state) {
            state.controlled = controller ? 1 : 0;
            std::ranges::copy(controller.value_or(win::DeviceIdBytes{}),
                              std::begin(state.controller_id));
        });
    }
    void inject(const osdrop_input& input) override {
        if (osdrop::engine::InputBridge* bridge = input_.load()) {
            bridge->inject(input);
        }
    }
    void capture_began(const win::DeviceIdBytes& device,
                       const std::vector<osdrop_input>& release_here) override {
        if (osdrop::engine::InputBridge* bridge = input_.load()) {
            bridge->capture_began(release_here);
        }
        change_input([&](osdrop_engine_input_state& state) {
            state.capturing = 1;
            std::ranges::copy(device, std::begin(state.target_id));
            clear_notice(state);
        });
    }
    void capture_ended(const win::CaptureEnd& end) override {
        if (osdrop::engine::InputBridge* bridge = input_.load()) {
            bridge->capture_ended(end);
        }
        change_input([&](osdrop_engine_input_state& state) {
            state.capturing = 0;
            std::ranges::fill(state.target_id, std::uint8_t{0});
            if (end.reason == kRefused || end.reason == kPeerLost) {
                state.notice_reason = end.reason;
                state.notice_refused = end.refused;
                std::ranges::copy(end.device, std::begin(state.notice_device_id));
            } else {
                clear_notice(state);
            }
        });
    }
    /// From the input bridge's threads.
    void input_reachable(bool reachable) {
        change_input(
            [&](osdrop_engine_input_state& state) { state.reachable = reachable ? 1 : 0; });
    }
    void input_locked(bool locked) {
        change_input([&](osdrop_engine_input_state& state) { state.locked = locked ? 1 : 0; });
    }
    [[nodiscard]] osdrop_engine_input_state input_state() const {
        const std::scoped_lock lock(state_mutex_);
        return state_;
    }
    void dismiss_input_notice() {
        change_input([](osdrop_engine_input_state& state) { clear_notice(state); });
    }
    /// Where control events go once the bridge exists; it hears of the members connected so far.
    void set_input(osdrop::engine::InputBridge* bridge) {
        const std::scoped_lock lock(input_mutex_);
        input_ = bridge;
        watch();
    }

    void clipboard_fetch(const std::optional<win::ClipboardFetch>& fetch) override {
        if (!fetch) {
            send(blank_event(OSDROP_ENGINE_CLIPBOARD_FETCH_ENDED));
            return;
        }
        osdrop_engine_event event = blank_event(OSDROP_ENGINE_CLIPBOARD_FETCHING);
        std::ranges::copy(fetch->device, std::begin(event.device_id));
        copy_name(fetch->name, event.name);
        event.bytes_done = fetch->bytes_done;
        event.bytes_total = fetch->bytes_total;
        send(event);
    }

private:
    /// capture_ended reasons the shell tells the user about: refused, and the other computer gone.
    static constexpr std::uint8_t kRefused = 4;
    static constexpr std::uint8_t kPeerLost = 6;

    void send(const osdrop_engine_event& event) const { callback_(context_, &event); }

    template <typename Change>
    void change_input(Change&& change) {
        {
            const std::scoped_lock lock(state_mutex_);
            std::forward<Change>(change)(state_);
        }
        send(blank_event(OSDROP_ENGINE_INPUT_CHANGED));
    }

    static void clear_notice(osdrop_engine_input_state& state) {
        state.notice_reason = 0;
        state.notice_refused = 0;
        std::ranges::fill(state.notice_device_id, std::uint8_t{0});
    }

    static win::DeviceIdBytes event_id(const osdrop_engine_event& event) {
        win::DeviceIdBytes id{};
        std::ranges::copy(event.device_id, id.begin());
        return id;
    }

    /// This PC's input is watched while a member is connected; under input_mutex_.
    void watch() const {
        if (osdrop::engine::InputBridge* bridge = input_.load()) {
            bridge->set_watching(!connected_.empty());
        }
    }

    static osdrop_engine_event_kind kind_of(win::ClipboardNotice notice) {
        switch (notice) {
        case win::ClipboardNotice::TooLarge:
            return OSDROP_ENGINE_CLIPBOARD_TOO_LARGE;
        case win::ClipboardNotice::Ready:
            return OSDROP_ENGINE_CLIPBOARD_READY;
        case win::ClipboardNotice::Unavailable:
            break;
        }
        return OSDROP_ENGINE_CLIPBOARD_UNAVAILABLE;
    }

    osdrop_engine_callback callback_;
    void* context_;
    std::atomic<osdrop::engine::InputBridge*> input_{nullptr};
    std::mutex input_mutex_;
    std::set<win::DeviceIdBytes> connected_; ///< members with a session; under input_mutex_
    mutable std::mutex state_mutex_;
    osdrop_engine_input_state state_{}; ///< under state_mutex_
};

win::DeviceIdBytes read_id(const std::uint8_t* bytes) {
    win::DeviceIdBytes id{};
    std::copy_n(bytes, id.size(), id.begin());
    return id;
}

} // namespace

/// The C handle: storage, DNS-SD, the clipboard and the shell's callback outlive the group engine
/// that uses them, so they are declared first.
struct osdrop_engine {
    osdrop_engine(osdrop_engine_callback callback, void* context) : events(callback, context) {}
    ~osdrop_engine() {
        try {
            if (input) {
                input->stop(); // its thread calls the group engine: it stops before it goes
            }
            clipboard.stop(); // its thread runs the group's clipboard work: it stops first
        } catch (...) {
            std::terminate(); // only joining can throw: unrecoverable
        }
    }
    osdrop_engine(const osdrop_engine&) = delete;
    osdrop_engine& operator=(const osdrop_engine&) = delete;
    osdrop_engine(osdrop_engine&&) = delete;
    osdrop_engine& operator=(osdrop_engine&&) = delete;

    osdrop::engine::DpapiStore store;
    osdrop::engine::DnsSdDiscovery discovery;
    osdrop::engine::WinClipboard clipboard;
    CallbackEvents events;
    std::unique_ptr<osdrop::engine::InputBridge> input;
    std::unique_ptr<win::GroupEngine> group; // declared last: stopped first

    /// Reads this PC's monitors and reports them, to core and to the input bridge.
    [[nodiscard]] osdrop_result report_screens() const {
        const auto monitors = osdrop::engine::physical_monitors();
        input->set_monitors(monitors);
        const auto report = win::screen_report(monitors);
        if (!report) {
            return report.error();
        }
        return group->set_screens(report.value());
    }
};

extern "C" {

osdrop_version osdrop_engine_core_version() {
    return osdrop_core_version();
}

osdrop_result osdrop_engine_create(const osdrop_engine_config* config,
                                   osdrop_engine_callback callback, void* context,
                                   osdrop_engine** out_engine) {
    if (config == nullptr || callback == nullptr || out_engine == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    *out_engine = nullptr;
    auto engine =
        std::unique_ptr<osdrop_engine>(new (std::nothrow) osdrop_engine(callback, context));
    if (!engine) {
        return OSDROP_ERROR_INTERNAL;
    }
    win::ClipboardLimits limits;
    if (config->clipboard_files_limit != 0) {
        limits.files = config->clipboard_files_limit;
    }
    auto group = win::GroupEngine::create(
        {.device_name = chosen_name(config->device_name),
         .random = osdrop_random_source{.fill = system_random, .context = nullptr},
         .clipboard = &engine->clipboard,
         .shares_clipboard = config->shares_clipboard == 1,
         .clipboard_limits = limits},
        engine->store, engine->discovery, engine->events);
    if (!group) {
        return group.error();
    }
    engine->group = std::move(group.value());
    if (engine->group->set_input_settings(config->input) != OSDROP_OK) {
        engine->events.log(OSDROP_LOG_WARNING, "The stored input settings were refused");
    }
    CallbackEvents* events = &engine->events;
    engine->input = std::make_unique<osdrop::engine::InputBridge>(
        *engine->group,
        osdrop::engine::InputBridge::Listener{
            .reachable = [events](bool reachable) { events->input_reachable(reachable); },
            .locked = [events](bool locked) { events->input_locked(locked); }});
    engine->events.set_input(engine->input.get());
    engine->clipboard.listen(
        [sync = engine->group->clipboard()](win::ClipData data) { sync->copied(std::move(data)); });
    if (engine->report_screens() != OSDROP_OK) {
        engine->events.log(OSDROP_LOG_WARNING, "Reporting the monitors failed");
    }
    *out_engine = engine.release();
    return OSDROP_OK;
}

void osdrop_engine_destroy(osdrop_engine* engine) {
    delete engine; // NOLINT(cppcoreguidelines-owning-memory): the C API owns the handle
}

osdrop_result osdrop_engine_get_info(osdrop_engine* engine, osdrop_engine_info* out_info) {
    if (engine == nullptr || out_info == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const win::EngineInfo info = engine->group->info();
    *out_info = osdrop_engine_info{.device_id = {},
                                   .group_id = {},
                                   .has_group = static_cast<std::uint8_t>(info.has_group),
                                   .listen_port = info.listen_port};
    std::ranges::copy(info.device_id, std::begin(out_info->device_id));
    std::ranges::copy(info.group_id, std::begin(out_info->group_id));
    return OSDROP_OK;
}

osdrop_result osdrop_engine_members(osdrop_engine* engine, osdrop_member* out, size_t capacity,
                                    size_t* out_count) {
    if (engine == nullptr || (out == nullptr && capacity != 0) || out_count == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const auto members = engine->group->members();
    if (!members) {
        return members.error();
    }
    *out_count = members.value().size();
    if (members.value().size() > capacity) {
        return OSDROP_ERROR_BUFFER_TOO_SMALL;
    }
    std::ranges::copy(members.value(), out);
    return OSDROP_OK;
}

osdrop_result osdrop_engine_nearby(osdrop_engine* engine, osdrop_nearby_computer* out,
                                   size_t capacity, size_t* out_count) {
    if (engine == nullptr || (out == nullptr && capacity != 0) || out_count == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const auto nearby = engine->group->nearby();
    *out_count = nearby.size();
    if (nearby.size() > capacity) {
        return OSDROP_ERROR_BUFFER_TOO_SMALL;
    }
    const std::span<osdrop_nearby_computer> output(out, capacity);
    for (std::size_t i = 0; i < nearby.size(); ++i) {
        osdrop_nearby_computer& entry = output[i];
        entry = osdrop_nearby_computer{
            .device_id = {},
            .platform = nearby[i].platform,
            .in_another_group = static_cast<std::uint8_t>(nearby[i].in_another_group),
            .needs_update = static_cast<std::uint8_t>(nearby[i].needs_update),
            .name = {}};
        std::ranges::copy(nearby[i].device_id, std::begin(entry.device_id));
        copy_name(nearby[i].name, entry.name);
    }
    return OSDROP_OK;
}

osdrop_result osdrop_engine_pair(osdrop_engine* engine, const uint8_t* device_id,
                                 uint64_t* out_connection) {
    if (engine == nullptr || device_id == nullptr || out_connection == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const auto connection = engine->group->pair(read_id(device_id));
    if (!connection) {
        return connection.error();
    }
    *out_connection = connection.value();
    return OSDROP_OK;
}

osdrop_result osdrop_engine_confirm_pairing(osdrop_engine* engine, uint64_t connection,
                                            uint8_t accepted) {
    if (engine == nullptr || accepted > 1) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    return engine->group->confirm_pairing(connection, accepted == 1);
}

osdrop_result osdrop_engine_revoke(osdrop_engine* engine, const uint8_t* device_id) {
    if (engine == nullptr || device_id == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    return engine->group->revoke(read_id(device_id));
}

osdrop_result osdrop_engine_block(osdrop_engine* engine, const uint8_t* device_id) {
    if (engine == nullptr || device_id == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    return engine->group->block(read_id(device_id));
}

osdrop_result osdrop_engine_unblock(osdrop_engine* engine, const uint8_t* device_id) {
    if (engine == nullptr || device_id == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    return engine->group->unblock(read_id(device_id));
}

osdrop_result osdrop_engine_blocked(osdrop_engine* engine, osdrop_blocked_device* out,
                                    size_t capacity, size_t* out_count) {
    if (engine == nullptr || (out == nullptr && capacity != 0) || out_count == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const auto blocked = engine->group->blocked();
    if (!blocked) {
        return blocked.error();
    }
    *out_count = blocked.value().size();
    if (blocked.value().size() > capacity) {
        return OSDROP_ERROR_BUFFER_TOO_SMALL;
    }
    std::ranges::copy(blocked.value(), out);
    return OSDROP_OK;
}

osdrop_result osdrop_engine_leave_group(osdrop_engine* engine) {
    if (engine == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    return engine->group->leave_group();
}

osdrop_result osdrop_engine_screens_changed(osdrop_engine* engine) {
    if (engine == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    return engine->report_screens();
}

osdrop_result osdrop_engine_close_all(osdrop_engine* engine, uint8_t sleeping) {
    if (engine == nullptr || sleeping > 1) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    engine->group->close_all(sleeping == 1);
    return OSDROP_OK;
}

osdrop_result osdrop_engine_resume(osdrop_engine* engine) {
    if (engine == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    engine->group->resume();
    return OSDROP_OK;
}

osdrop_result osdrop_engine_cancel_clipboard_fetch(osdrop_engine* engine) {
    if (engine == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    if (win::ClipboardSync* sync = engine->group->clipboard()) {
        sync->cancel_fetch();
    }
    return OSDROP_OK;
}

osdrop_result osdrop_engine_set_clipboard(osdrop_engine* engine, uint8_t shared,
                                          uint64_t files_limit) {
    if (engine == nullptr || shared > 1 || files_limit == 0) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    engine->group->set_clipboard_sharing(shared == 1, files_limit);
    return OSDROP_OK;
}

osdrop_result osdrop_engine_set_permissions(osdrop_engine* engine, const uint8_t* device_id,
                                            const osdrop_permissions* permissions) {
    if (engine == nullptr || device_id == nullptr || permissions == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    return engine->group->set_permissions(read_id(device_id), *permissions);
}

osdrop_result osdrop_engine_layout(osdrop_engine* engine, osdrop_layout_computer* out,
                                   size_t capacity, size_t* out_count) {
    if (engine == nullptr || (out == nullptr && capacity != 0) || out_count == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const auto layout = engine->group->layout();
    if (!layout) {
        return layout.error();
    }
    *out_count = layout.value().size();
    if (layout.value().size() > capacity) {
        return OSDROP_ERROR_BUFFER_TOO_SMALL;
    }
    std::ranges::copy(layout.value(), out);
    return OSDROP_OK;
}

osdrop_result osdrop_engine_set_layout(osdrop_engine* engine, const osdrop_placement* placements,
                                       size_t count) {
    if (engine == nullptr || (placements == nullptr && count != 0)) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    return engine->group->set_layout(std::span(placements, count));
}

osdrop_result osdrop_engine_set_input_settings(osdrop_engine* engine,
                                               const osdrop_input_settings* settings) {
    if (engine == nullptr || settings == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    return engine->group->set_input_settings(*settings);
}

osdrop_result osdrop_engine_set_pointer_speed(osdrop_engine* engine, const uint8_t* device_id,
                                              uint32_t permille) {
    if (engine == nullptr || device_id == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    return engine->group->set_pointer_speed(read_id(device_id), permille);
}

osdrop_result osdrop_engine_lock_input(osdrop_engine* engine, uint8_t locked) {
    if (engine == nullptr || locked > 1) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    engine->input->set_locked(locked == 1);
    return OSDROP_OK;
}

osdrop_result osdrop_engine_get_input_state(osdrop_engine* engine,
                                            osdrop_engine_input_state* out_state) {
    if (engine == nullptr || out_state == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    *out_state = engine->events.input_state();
    return OSDROP_OK;
}

osdrop_result osdrop_engine_dismiss_input_notice(osdrop_engine* engine) {
    if (engine == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    engine->events.dismiss_input_notice();
    return OSDROP_OK;
}

} // extern "C"
