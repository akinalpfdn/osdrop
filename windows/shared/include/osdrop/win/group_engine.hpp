#pragma once

// The Windows engine's group logic, free of Windows headers so it is tested and linted on every
// host: it starts the mesh from the stored identity and states, connects discovery to it, keeps
// the nearby computers, stores what core hands over and advertises the group. The engine supplies
// DNS-SD and storage behind the interfaces below (windows/engine).

#include <osdrop/osdrop.h>
#include <osdrop/win/clipboard_sync.hpp>
#include <osdrop/win/expected.hpp>

#include <array>
#include <cstdint>
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
};

/// What a PC can do so far: share the clipboard and receive files. Taking control comes with the
/// input process (DEVPLAN Phase 3), drags with the drag bridge (Phase 4); announcing them earlier
/// would let a Mac's cursor cross into a PC that cannot follow it.
inline constexpr std::uint32_t kAvailableFeatures = OSDROP_FEATURE_CLIPBOARD | OSDROP_FEATURE_FILES;

class GroupEngine {
public:
    struct Config {
        std::string device_name; ///< 1 to 64 bytes of UTF-8
        osdrop_random_source random{};
        /// The clipboard to share; none: the clipboard is not shared. Outlives the engine.
        ClipboardHost* clipboard = nullptr;
        /// What this computer announces to the members (OSDROP_FEATURE_* bits).
        std::uint32_t features = kAvailableFeatures;
    };

    /// Fails with UNSUPPORTED_VERSION for a group stored by a newer Osdrop (it is kept, not
    /// overwritten) and with another error for a damaged identity or group state. A damaged
    /// arrangement is dropped and rebuilt.
    [[nodiscard]] static Expected<std::unique_ptr<GroupEngine>, osdrop_result>
    create(Config config, StateStore& store, Discovery& discovery, EngineEvents& events);

    /// Stops discovery, then ends every session (Bye: application quit).
    ~GroupEngine();
    GroupEngine(const GroupEngine&) = delete;
    GroupEngine& operator=(const GroupEngine&) = delete;
    GroupEngine(GroupEngine&&) = delete;
    GroupEngine& operator=(GroupEngine&&) = delete;

    // Commands: from any thread, never from inside an EngineEvents callback.
    [[nodiscard]] EngineInfo info() const;
    [[nodiscard]] Expected<std::vector<osdrop_member>, osdrop_result> members() const;
    [[nodiscard]] std::vector<NearbyComputer> nearby() const;
    /// Dials a nearby computer to pair; the result identifies the pairing's events.
    [[nodiscard]] Expected<std::uint64_t, osdrop_result> pair(const DeviceIdBytes& device);
    [[nodiscard]] osdrop_result confirm_pairing(std::uint64_t connection, bool accepted);
    [[nodiscard]] osdrop_result revoke(const DeviceIdBytes& device);
    [[nodiscard]] osdrop_result leave_group();
    [[nodiscard]] osdrop_result set_screens(std::span<const osdrop_monitor> monitors);
    /// Ends every session before sleep (peers see PEER_SLEEPING) or quitting.
    void close_all(bool sleeping);
    /// After waking: browses again, so members are found and dialled again.
    void resume();
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

    Config config_;
    StateStore& store_;
    Discovery& discovery_;
    EngineEvents& events_;
    osdrop_mesh* mesh_ = nullptr;
    std::unique_ptr<ClipboardSync> clipboard_;
    std::unique_ptr<CoreClipboardMesh> clipboard_mesh_;
    DeviceIdBytes self_{}; ///< set before port_, which publishes it

    mutable std::mutex mutex_;
    std::uint16_t port_ = 0;                               ///< 0 until started; guarded by mutex_
    std::optional<GroupIdBytes> group_;                    ///< guarded by mutex_
    std::map<std::string, DiscoveredComputer> discovered_; ///< by label; guarded by mutex_
};

} // namespace osdrop::win
