#pragma once

// The Windows engine's group logic, free of Windows headers so it is tested and linted on every
// host: it starts the mesh from the stored identity and states, connects discovery to it, keeps
// the nearby computers, stores what core hands over and advertises the group. The engine supplies
// DNS-SD and storage behind the interfaces below (windows/engine).

#include <osdrop/osdrop.h>
#include <osdrop/win/clipboard_sync.hpp>
#include <osdrop/win/expected.hpp>
#include <osdrop/win/network_drag.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace osdrop::win {

using DeviceIdBytes = std::array<std::uint8_t, OSDROP_DEVICE_ID_SIZE>;
using GroupIdBytes = std::array<std::uint8_t, OSDROP_GROUP_ID_SIZE>;

/// An all-empty advertisement with a valid platform value.
inline osdrop_advertisement blank_advertisement() {
    return {.protocol_version = 0,
            .platform = OSDROP_PLATFORM_WINDOWS,
            .protocol_minor = 0,
            .has_group = 0,
            .device_id = {},
            .group_id = {},
            .name = {}};
}

/// A computer found over DNS-SD and resolved to an address.
struct DiscoveredComputer {
    std::string label; ///< identifies it in `lost`
    osdrop_advertisement advertisement = blank_advertisement();
    std::string address; ///< IPv4 or IPv6 literal, or a .local host name
    std::uint16_t port = 0;
};

class Discovery {
public:
    struct Callbacks {
        std::function<void(const DiscoveredComputer&)> found; ///< also when its record changes
        std::function<void(const std::string& label)> lost;
    };

    virtual ~Discovery() = default;
    /// Registers this computer, replacing an earlier registration; false when DNS-SD refused.
    virtual bool advertise(const osdrop_advertisement& advertisement, std::uint16_t port) = 0;
    /// Starts browsing, or starts over; callbacks run one at a time on other threads.
    virtual bool browse(Callbacks callbacks) = 0;
    /// Ends browsing and the registration; no callback runs after it returns.
    virtual void stop() = 0;
};

enum class StoredItem : std::uint8_t {
    Identity,    ///< the 32-byte secret key; secret
    GroupState,  ///< core's group bytes; decides who may connect
    LayoutState, ///< core's arrangement bytes
    ListenPort,  ///< two bytes, little-endian
};

class StateStore {
public:
    virtual ~StateStore() = default;
    /// The stored bytes, nullopt when there are none, or an error when they cannot be read.
    [[nodiscard]] virtual Expected<std::optional<std::vector<std::uint8_t>>, osdrop_result>
    load(StoredItem item) = 0;
    [[nodiscard]] virtual bool save(StoredItem item, std::span<const std::uint8_t> bytes) = 0;
};

/// A computer nearby that is not a member of this computer's group.
struct NearbyComputer {
    DeviceIdBytes device_id{};
    std::string name;
    osdrop_platform platform = OSDROP_PLATFORM_WINDOWS;
    bool in_another_group = false; ///< pairing is refused until one of the two leaves its group
    bool needs_update = false;     ///< another protocol version
};

struct EngineInfo {
    DeviceIdBytes device_id{};
    GroupIdBytes group_id{};
    bool has_group = false;
    std::uint16_t listen_port = 0;
};

/// How the cursor came back to this PC, as osdrop_mesh_callbacks.capture_ended reports it.
struct CaptureEnd {
    DeviceIdBytes device{};
    std::int32_t x = 0; ///< where the cursor shows, in 1/16 physical pixel
    std::int32_t y = 0;
    std::uint8_t reason = 0;  ///< 1 pushed back, 2 hotkey, 3 capture lost, 4 refused, 5 someone
                              ///< used the other computer, 6 it stopped answering or left
    std::uint8_t refused = 0; ///< reason 4: 1 locked, 2 asleep, 3 not permitted, 4 busy
    bool caps_lock = false;   ///< as the user left it on the other computers
};

/// A drag that came to this PC with the cursor (osdrop_mesh_callbacks.drag_arrived).
struct DragArrival {
    DeviceIdBytes source{}; ///< where its items are fetched from
    std::uint32_t drag_id = 0;
    std::int32_t x = 0; ///< where it starts, in 1/16 physical pixel
    std::int32_t y = 0;
    std::vector<drag::ArrivingItem> items;
};

/// A member fetching an item of this PC's drag (osdrop_mesh_callbacks.send_progress).
struct SendUpdate {
    DeviceIdBytes device{};
    std::uint32_t drag_id = 0;
    std::uint32_t index = 0;
    std::string name; ///< the item's name, empty when not known
    std::uint64_t sent = 0;
    std::uint64_t total = 0;
    std::uint8_t state = 1; ///< 1 sending, 2 every byte went out, 3 stopped
};

/// What the shell hears, on core's or DNS-SD's threads. Callbacks must return quickly and must
/// not call the engine back; queue the work instead.
class EngineEvents {
public:
    virtual ~EngineEvents() = default;
    /// The group or a member changed: read members() again.
    virtual void group_changed() = 0;
    /// The nearby computers changed: read nearby() again.
    virtual void nearby_changed() = 0;
    /// OSDROP_OK when this computer left, NOT_TRUSTED when a member removed it.
    virtual void left_group(osdrop_result reason) = 0;
    virtual void pairing_code(std::uint64_t connection, std::uint32_t code,
                              const osdrop_peer& peer) = 0;
    virtual void pairing_failed(std::uint64_t connection, osdrop_result reason) = 0;
    /// `features`: what the member announces (OSDROP_FEATURE_* bits).
    virtual void peer_connected(const osdrop_peer& peer, std::uint32_t features) = 0;
    virtual void peer_disconnected(const DeviceIdBytes& device, osdrop_result reason) = 0;
    virtual void peer_responsive(const DeviceIdBytes& device, bool responsive) = 0;
    virtual void stats(const DeviceIdBytes& device, const osdrop_session_stats& stats) = 0;
    virtual void log(osdrop_log_level level, const char* message) = 0;
    /// Another computer's copy is not on the clipboard, or its files are now; `name` is empty
    /// when not known.
    virtual void clipboard_notice(ClipboardNotice notice, const DeviceIdBytes& device,
                                  const std::string& name) = 0;
    /// Copied files on their way from another computer; nullopt once they no longer are.
    virtual void clipboard_fetch(const std::optional<ClipboardFetch>& fetch) = 0;
    /// The arrangement or a member's screens changed: read layout() again.
    virtual void layout_changed() = 0;
    /// The member that controls this computer now; nullopt once no member does.
    virtual void controlled(const std::optional<DeviceIdBytes>& controller) = 0;
    /// Input from the controlling computer, to inject here.
    virtual void inject(const osdrop_input& input) = 0;
    /// The cursor went to `device`: capture this PC's input. The keys in `release_here` (KEY
    /// inputs, action 2) went down here; their release now goes to the other computer.
    virtual void capture_began(const DeviceIdBytes& device,
                               const std::vector<osdrop_input>& release_here) = 0;
    /// The cursor came back: give this PC its input again.
    virtual void capture_ended(const CaptureEnd& end) = 0;
    /// A drag came with the cursor: start it where it entered, pressing the left button there,
    /// and report its end with drop_finished() (DEVPLAN Phase 14).
    virtual void drag_arrived(const DragArrival& arrival) = 0;
    /// The drag that arrived left with the cursor, or cannot go on: end it without a drop.
    virtual void drag_withdrawn(std::uint32_t drag_id) = 0;
    /// This PC's drag waits at the edge where it left: release it there, as a copy when it was
    /// dropped elsewhere, refused otherwise.
    virtual void drag_ended(std::uint32_t drag_id, osdrop_drag_outcome outcome) = 0;
    /// This PC's drag of virtual files is wanted elsewhere: take them at the edge and report
    /// them with drag_started(), or report no files.
    virtual void drag_wanted() = 0;
    /// A transfer this PC receives moved on, or ended (clipboard ones included).
    virtual void transfer_progress(std::uint64_t transfer_id, std::uint64_t done,
                                   std::uint64_t total) = 0;
    virtual void transfer_finished(std::uint64_t transfer_id, osdrop_result result) = 0;
    /// A member fetches an item of this PC's drag.
    virtual void send_progress(const SendUpdate& update) = 0;
};

/// What a PC can do: be controlled through the input process (DEVPLAN Phase 3), share the
/// clipboard, receive files, and send and take drags (Phases 4 and 14).
inline constexpr std::uint32_t kAvailableFeatures =
    OSDROP_FEATURE_TAKES_CONTROL | OSDROP_FEATURE_CLIPBOARD | OSDROP_FEATURE_FILES |
    OSDROP_FEATURE_DRAG_SOURCE | OSDROP_FEATURE_DRAG_TARGET;

class GroupEngine final : public drag::DragTransfers {
public:
    struct Config {
        std::string device_name; ///< 1 to 64 bytes of UTF-8
        osdrop_random_source random{};
        /// The clipboard to share; none: the clipboard is not shared. Outlives the engine.
        ClipboardHost* clipboard = nullptr;
        /// The user's clipboard settings; set_clipboard_sharing() changes them later.
        bool shares_clipboard = true;
        ClipboardLimits clipboard_limits{};
        /// What this computer announces to the members (OSDROP_FEATURE_* bits).
        std::uint32_t features = kAvailableFeatures;
    };

    /// Fails with UNSUPPORTED_VERSION for a group stored by a newer Osdrop (it is kept, not
    /// overwritten) and with another error for a damaged identity or group state. A damaged
    /// arrangement is dropped and rebuilt.
    [[nodiscard]] static Expected<std::unique_ptr<GroupEngine>, osdrop_result>
    create(Config config, StateStore& store, Discovery& discovery, EngineEvents& events);

    /// Stops discovery, then ends every session (Bye: application quit).
    ~GroupEngine() override;
    GroupEngine(const GroupEngine&) = delete;
    GroupEngine& operator=(const GroupEngine&) = delete;
    GroupEngine(GroupEngine&&) = delete;
    GroupEngine& operator=(GroupEngine&&) = delete;

    // Commands: from any thread, never from inside an EngineEvents callback.
    [[nodiscard]] EngineInfo info() const;
    [[nodiscard]] Expected<std::vector<osdrop_member>, osdrop_result> members() const;
    [[nodiscard]] Expected<std::vector<osdrop_blocked_device>, osdrop_result> blocked() const;
    /// Computers nearby that are neither members nor blocked.
    [[nodiscard]] std::vector<NearbyComputer> nearby() const;
    /// Dials a nearby computer to pair; the result identifies the pairing's events.
    [[nodiscard]] Expected<std::uint64_t, osdrop_result> pair(const DeviceIdBytes& device);
    [[nodiscard]] osdrop_result confirm_pairing(std::uint64_t connection, bool accepted);
    [[nodiscard]] osdrop_result revoke(const DeviceIdBytes& device);
    /// As osdrop_mesh_block and osdrop_mesh_unblock.
    [[nodiscard]] osdrop_result block(const DeviceIdBytes& device);
    [[nodiscard]] osdrop_result unblock(const DeviceIdBytes& device);
    [[nodiscard]] osdrop_result leave_group();
    [[nodiscard]] osdrop_result set_screens(std::span<const osdrop_monitor> monitors);
    /// As osdrop_mesh_layout and osdrop_mesh_set_layout: the arrangement, and a new one from the
    /// editor.
    [[nodiscard]] Expected<std::vector<osdrop_layout_computer>, osdrop_result> layout() const;
    [[nodiscard]] osdrop_result set_layout(std::span<const osdrop_placement> placements);
    /// As osdrop_mesh_set_permissions; group_changed follows.
    [[nodiscard]] osdrop_result set_permissions(const DeviceIdBytes& device,
                                                const osdrop_permissions& permissions);
    /// As osdrop_mesh_set_input_settings and osdrop_mesh_set_pointer_speed.
    [[nodiscard]] osdrop_result set_input_settings(const osdrop_input_settings& settings);
    [[nodiscard]] osdrop_result set_pointer_speed(const DeviceIdBytes& device,
                                                  std::uint32_t permille);
    /// The user's clipboard settings, as ClipboardSync::set_sharing.
    void set_clipboard_sharing(bool shared, std::uint64_t files_limit);
    /// As osdrop_mesh_input, osdrop_mesh_set_unavailable and osdrop_mesh_cursor_moved: this PC's
    /// own input, captured `age_us` ago, whether it can be controlled, and where something else
    /// moved its cursor.
    void physical_input(const osdrop_input& input, std::uint64_t age_us);
    void set_unavailable(std::uint8_t reason);
    void cursor_moved(std::int32_t x, std::int32_t y);
    /// As osdrop_mesh_return_home, osdrop_mesh_lock_input and osdrop_mesh_set_caps_lock.
    void return_home(bool input_lost);
    void lock_input(bool locked);
    void set_caps_lock(bool on);
    /// Ends every session before sleep (peers see PEER_SLEEPING) or quitting.
    void close_all(bool sleeping);
    /// After waking: browses again, so members are found and dialled again.
    void resume();
    /// This PC's drag, as osdrop_mesh_drag_started: its files and folders (absolute paths), each
    /// with the type its name gives it. The drag's ID, or nullopt when core refused it.
    [[nodiscard]] std::optional<std::uint32_t>
    drag_started(std::span<const std::filesystem::path> items);
    /// As osdrop_mesh_drag_promised, osdrop_mesh_drag_stopped and osdrop_mesh_drop_finished.
    void drag_promised();
    void drag_stopped();
    void drop_finished(std::uint32_t drag_id, osdrop_drag_outcome outcome);
    /// As osdrop_mesh_withdraw for a drag: members fetching its items stop.
    void withdraw_drag(std::uint32_t drag_id);
    // drag::DragTransfers: the items of drags that arrive.
    [[nodiscard]] std::optional<std::uint64_t>
    receive(const drag::SourceId& source, std::uint32_t drag_id, std::uint32_t index,
            const std::filesystem::path& folder,
            const std::weak_ptr<drag::TransferSink>& sink) override;
    void cancel(std::uint64_t transfer_id) override;
    /// The shared clipboard; null without a ClipboardHost. Its copied() runs on the clipboard
    /// thread.
    [[nodiscard]] ClipboardSync* clipboard() { return clipboard_.get(); }

private:
    GroupEngine(Config config, StateStore& store, Discovery& discovery, EngineEvents& events);
    [[nodiscard]] osdrop_result start();
    [[nodiscard]] Expected<std::vector<std::uint8_t>, osdrop_result> load_identity();
    [[nodiscard]] osdrop_result create_mesh(const std::vector<std::uint8_t>& secret,
                                            const std::optional<std::vector<std::uint8_t>>& group,
                                            const std::optional<std::vector<std::uint8_t>>& layout,
                                            std::uint16_t port);
    void advertise(const std::optional<GroupIdBytes>& group);
    void browse();
    void found(const DiscoveredComputer& computer);
    void lost(const std::string& label);
    [[nodiscard]] osdrop_mesh_callbacks callbacks();
    /// Core's callbacks for the cursor and keyboard across computers, and for drags.
    static void control_callbacks(osdrop_mesh_callbacks& result);
    static void drag_callbacks(osdrop_mesh_callbacks& result);

    Config config_;
    StateStore& store_;
    Discovery& discovery_;
    EngineEvents& events_;
    osdrop_mesh* mesh_ = nullptr;
    std::unique_ptr<ClipboardSync> clipboard_;
    std::unique_ptr<CoreClipboardMesh> clipboard_mesh_;
    drag::TransferRouter transfers_; ///< drag items' transfer events
    /// The item names of this PC's latest drags, for send_progress; guarded by mutex_.
    std::deque<std::pair<std::uint32_t, std::vector<std::string>>> drag_names_;
    DeviceIdBytes self_{}; ///< set before port_, which publishes it

    mutable std::mutex mutex_;
    std::uint16_t port_ = 0;                               ///< 0 until started; guarded by mutex_
    std::optional<GroupIdBytes> group_;                    ///< guarded by mutex_
    std::map<std::string, DiscoveredComputer> discovered_; ///< by label; guarded by mutex_
};

} // namespace osdrop::win
