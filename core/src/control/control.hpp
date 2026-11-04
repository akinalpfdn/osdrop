#pragma once

// Cursor and keyboard across computers (DEVPLAN, "Roles and topology" and Phase 11;
// docs/PROTOCOL.md, "Input control messages"). Every computer plays two parts:
// - Controller: the platform reports all of its physical input. While the cursor is home the
//   crossing engine only watches for a push through an edge; once the cursor is on another
//   computer, the platform captures the input and this layer sends it there.
// - Endpoint: input from the one controller that entered is handed to the platform to inject.
//   Whatever was injected and is still held is released when control ends, however it ends.
// Local input always wins: physical input on a controlled computer ends the control at once. A
// controller that loses the computer with the cursor (it stops answering, leaves, or stops
// accepting control) brings the cursor home to where it left.
//
// The layer sits after the clipboard in the mesh's observer chain: it keeps input messages and
// motion datagrams to itself and passes everything else on to `next`. It also stands between the
// layout and its observer, to follow the arrangement.

#include "control/held_inputs.hpp"
#include "layout/crossing.hpp"
#include "layout/edge_push.hpp"
#include "layout/layout.hpp"
#include "mesh/mesh.hpp"
#include "protocol/control_messages.hpp"
#include "util/time.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace osdrop::control {

using layout::Point;
using mesh::DeviceId;

// --- Physical input on this computer, as the platform captured it ---

/// `position` is where this computer's cursor is (held in place while the input is captured),
/// `delta` the movement; both native, in coord units.
struct PointerMoved {
    Point position;
    Point delta;
};
struct KeyInput {
    HeldKey key;
    protocol::KeyAction action = protocol::KeyAction::Down;
};
/// `click_count` comes from the platform's own double-click settings (docs/PROTOCOL.md, 0x24).
struct ButtonInput {
    std::uint8_t button = 0;
    protocol::ButtonAction action = protocol::ButtonAction::Down;
    std::uint8_t click_count = 1;
};
struct ScrollInput {
    protocol::ScrollUnit unit = protocol::ScrollUnit::Lines;
    protocol::ScrollPhase phase = protocol::ScrollPhase::None;
    protocol::MomentumPhase momentum = protocol::MomentumPhase::None;
    std::int32_t dx = 0; ///< 1/256 of a unit; positive moves content right
    std::int32_t dy = 0; ///< positive moves content down
};
using Input = std::variant<PointerMoved, KeyInput, ButtonInput, ScrollInput>;

// --- What the platform does ---

/// Moves this computer's cursor to a native point.
struct MoveCursor {
    Point position;
};
/// Input for the platform to apply as if this computer's own devices produced it; the control_id
/// fields carry no meaning here.
using Injection =
    std::variant<MoveCursor, protocol::KeyEvent, protocol::ButtonEvent, protocol::ScrollEvent>;

/// Why the cursor came back to this computer.
enum class CaptureEnd : std::uint8_t {
    Returned = 1,  ///< pushed back through an edge
    Hotkey = 2,    ///< the user asked for it
    InputLost = 3, ///< the platform lost its capture
    Refused = 4,   ///< the other computer refused it
    Takeover = 5,  ///< someone used the other computer's own mouse or keyboard
    PeerLost = 6,  ///< the other computer stopped answering, left, or stopped accepting control
};

struct CaptureEnded {
    DeviceId device{}; ///< the computer the cursor was on
    Point cursor;      ///< where this computer's cursor goes
    CaptureEnd reason = CaptureEnd::Returned;
    std::optional<protocol::RefuseReason> refused;
};

/// Motion latency on this computer while it is controlled, from the controller's capture to the
/// arrival here, through the session clock offset.
struct LatencyStats {
    std::size_t samples = 0;
    Microseconds median{0};
    Microseconds p99{0};
    bool operator==(const LatencyStats&) const = default;
};

class ControlObserver {
public:
    virtual ~ControlObserver() = default;
    /// Controller: capture this computer's input and hide its cursor. The keys in `release_here`
    /// went down on this computer; release them here (their release now goes elsewhere).
    virtual void on_capture_began(const DeviceId& device,
                                  std::span<const HeldKey> release_here) = 0;
    /// Controller: give the input back and show the cursor at `ended.cursor`.
    virtual void on_capture_ended(const CaptureEnded& ended) = 0;
    /// Endpoint: `controller` controls this computer now; nothing when control ended.
    virtual void on_controlled(const std::optional<DeviceId>& controller) = 0;
    virtual void on_inject(const Injection& injection) = 0;
    /// Endpoint, about once per session statistics interval while motion arrives.
    virtual void on_input_latency(const DeviceId& controller, const LatencyStats& stats) = 0;
};

struct ControlTunables {
    layout::PushConfig push;
    /// Motion latencies kept for the median and the 99th percentile.
    std::size_t latency_samples = 1000;
    /// An endpoint reports its cursor when something else moved it further than this.
    std::int64_t cursor_report_distance = layout::kUnit;
};

class Control final : public mesh::MeshObserver, public layout::LayoutObserver {
public:
    Control(const DeviceId& self, const ControlTunables& tunables, ControlObserver& observer,
            layout::LayoutObserver& layout_observer, mesh::MeshObserver& next);
    ~Control() override = default;
    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;
    Control(Control&&) = delete;
    Control& operator=(Control&&) = delete;

    /// The mesh to send through and the layout to follow; set once, before the mesh delivers
    /// anything.
    void attach(mesh::Mesh& mesh, const layout::Layout& layout);

    // --- Commands, on the mesh's thread; flush the mesh afterwards ---
    /// The members that may control this computer (their control permission is on).
    void set_controllers(std::set<DeviceId> controllers);
    /// Physical input on this computer, captured at `captured`.
    void input(const Input& input, TimePoint captured);
    /// Brings the cursor home: the user's hotkey (Leave reason 3) or a capture the platform lost
    /// (reason 4).
    void return_home(protocol::LeaveReason reason);
    /// "Lock input here": while locked, the cursor stays on the computer it is on.
    void set_locked(bool locked);
    /// Endpoint: this computer cannot be controlled now (its screen is locked, it is going to
    /// sleep); nothing when it can again. A computer being controlled is released.
    void set_unavailable(std::optional<protocol::RefuseReason> reason);
    /// Endpoint: where this computer's cursor really is; reported when something else moved it.
    void cursor_moved(Point position);

    [[nodiscard]] bool capturing() const { return away_.has_value(); }
    [[nodiscard]] std::optional<DeviceId> controller() const;

    // --- layout::LayoutObserver ---
    void on_layout_changed(const layout::LayoutState& state) override;

    // --- mesh::MeshObserver ---
    mesh::TrustVerdict on_trust_query(const crypto::Key& peer_key,
                                      const protocol::Hello& hello) override;
    void on_pairing_code(mesh::ConnectionId connection, std::uint32_t code,
                         const crypto::Key& peer_key, const protocol::Hello& hello) override;
    void on_paired(const crypto::Key& peer_key, const protocol::Hello& hello) override;
    void on_pairing_failed(mesh::ConnectionId connection, Error reason) override;
    void on_peer_connected(const DeviceId& device, const crypto::Key& peer_key,
                           const protocol::Hello& hello,
                           const protocol::Capabilities& capabilities) override;
    void on_peer_disconnected(const DeviceId& device, std::optional<Error> reason,
                              std::optional<protocol::ByeReason> bye) override;
    void on_peer_capabilities(const DeviceId& device,
                              const protocol::Capabilities& capabilities) override;
    void on_peer_responsive(const DeviceId& device, bool responsive) override;
    void on_message(const DeviceId& device, const protocol::ControlMessage& message) override;
    void on_motion(const DeviceId& device, const protocol::PointerMotion& motion) override;
    void on_transfer_opened(const DeviceId& device, std::uint32_t stream_id) override;
    void on_transfer_message(const DeviceId& device, std::uint32_t stream_id,
                             const protocol::TransferMessage& message) override;
    void on_transfer_writable(const DeviceId& device, std::uint32_t stream_id) override;
    void on_transfer_closed(const DeviceId& device, std::uint32_t stream_id,
                            std::optional<Error> reason) override;
    void on_stats(const DeviceId& device, const mesh::SessionStats& stats) override;
    void on_log(mesh::LogLevel level, std::string_view message) override;

private:
    /// A computer the cursor can be on: connected, answering, and accepting control from this one.
    struct Peer {
        bool responsive = true;
        bool accepts_control = false;
    };
    /// The computer this one's input goes to.
    struct Away {
        DeviceId device{};
        std::uint32_t control_id = 0;
        Point sent; ///< the last position sent
    };
    /// The controller whose input this computer injects.
    struct Controlled {
        DeviceId controller{};
        std::uint32_t control_id = 0;
        HeldInputs held;
        Point position; ///< where the controller last put the cursor
    };

    // Controller.
    void moved(const PointerMoved& moved, TimePoint captured);
    void cross(const DeviceId& device, Point position);
    void come_home(Point cursor, CaptureEnd reason, std::optional<protocol::RefuseReason> refused);
    void refresh();
    void update_lock();
    void receive_refusal(const DeviceId& device, std::uint32_t control_id, CaptureEnd reason,
                         std::optional<protocol::RefuseReason> refused);
    [[nodiscard]] std::uint32_t next_control_id();

    // Endpoint.
    void receive_enter(const DeviceId& device, const protocol::Enter& enter);
    [[nodiscard]] Controlled* controlled_by(const DeviceId& device, std::uint32_t control_id);
    void inject_key(Controlled& controlled, const protocol::KeyEvent& event);
    void inject_button(Controlled& controlled, const protocol::ButtonEvent& event);
    void receive_motion(Controlled& controlled, const protocol::PointerMotion& motion);
    void take_back();
    /// Releases what is held and ends the control; the controller is told nothing here.
    void release();
    void release_held(Controlled& controlled);
    void report_latency(const DeviceId& device);

    /// True for the input messages this layer handles.
    bool handle(const DeviceId& device, const protocol::ControlMessage& message);
    void send(const DeviceId& device, protocol::ControlMessage message);

    DeviceId self_;
    ControlTunables tunables_;
    ControlObserver& observer_;
    layout::LayoutObserver& layout_observer_;
    mesh::MeshObserver& next_;
    mesh::Mesh* mesh_ = nullptr;             // set by attach(); outlived by the mesh's owner
    const layout::Layout* layout_ = nullptr; // set by attach()

    std::map<DeviceId, Peer> peers_; ///< members with an established session
    std::set<DeviceId> controllers_;
    layout::CrossingEngine engine_;
    HeldInputs physical_; ///< this computer's own keys and buttons
    std::optional<Away> away_;
    std::uint32_t last_control_id_ = 0;
    bool locked_ = false;

    std::optional<Controlled> controlled_;
    std::optional<protocol::RefuseReason> unavailable_;
    std::map<DeviceId, std::int64_t> clock_offsets_; ///< peer clock minus this one, microseconds
    std::vector<std::int64_t> latencies_;            ///< microseconds, a ring of latency_samples
    std::size_t next_latency_ = 0;
    bool latency_changed_ = false;
};

} // namespace osdrop::control
