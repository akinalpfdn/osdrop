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
// Drags of files travel with the cursor (docs/PROTOCOL.md, "Drag messages"): the computer a drag
// started on offers its items to the transfers, the controller carries the drag across edges, and
// the computer it is dropped on fetches the items from there.
//
// The layer sits after the clipboard in the mesh's observer chain: it keeps input and drag
// messages and motion datagrams to itself and passes everything else on to `next`. It also stands
// between the layout and its observer, to follow the arrangement, and between the transfers and
// theirs, to learn the size of the items it offers.

#include "control/held_inputs.hpp"
#include "layout/crossing.hpp"
#include "layout/edge_push.hpp"
#include "layout/layout.hpp"
#include "mesh/mesh.hpp"
#include "protocol/control_messages.hpp"
#include "transfer/service.hpp"
#include "transfer/types.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"
#include "util/time.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
/// Turns Caps Lock on or off on this computer. Caps Lock travels as a state, not as key
/// presses, because platforms toggle it differently.
struct SetCapsLock {
    bool on = false;
};
/// Input for the platform to apply as if this computer's own devices produced it; the control_id
/// fields carry no meaning here.
using Injection = std::variant<MoveCursor, protocol::KeyEvent, protocol::ButtonEvent,
                               protocol::ScrollEvent, SetCapsLock>;

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
    bool caps_lock = false; ///< Caps Lock as the user left it on the other computers
};

/// A file or folder of a drag on this computer, as the platform found it.
struct DraggedItem {
    std::filesystem::path path; ///< absolute
    std::string content_type;   ///< its Uniform Type Identifier
};

/// A drag that comes to this computer: the platform starts a drag of `items` at `position`,
/// pressing the left button there, and fetches each item from `source` with the transfers (object
/// kind Drag, object_id `drag_id`, item_index its place in `items`) when the drop asks for it.
struct DragArrival {
    DeviceId source{};
    std::uint32_t drag_id = 0;
    std::vector<protocol::OfferedItem> items;
    Point position;
    bool operator==(const DragArrival&) const = default;
};

/// Motion latency on this computer while it is controlled, from the controller's capture to the
/// arrival here, through the session clock offset.
struct LatencyStats {
    std::size_t samples = 0;
    Microseconds median{0};
    Microseconds p99{0};
    bool operator==(const LatencyStats&) const = default;
};

/// The button a drag is made with: the left one.
inline constexpr std::uint8_t kDragButton = 0;

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
    /// Target: start a drag (see DragArrival); drop_finished() reports how it ended.
    virtual void on_drag_arrived(const DragArrival& arrival) = 0;
    /// Target: the drag left this computer with the cursor, or cannot go on; end it without a
    /// drop. A held left button is released after this.
    virtual void on_drag_withdrawn(std::uint32_t drag_id) = 0;
    /// Source: end this computer's drag, which waits at the edge where it left, by releasing the
    /// left button there: as a copy when it was dropped, sliding back otherwise.
    virtual void on_drag_ended(std::uint32_t drag_id, protocol::DragOutcome outcome) = 0;
    /// Source: this computer's drag of promised files is wanted on another computer. Take its
    /// files at the edge (ending the drag there) and report them with drag_started(), or an
    /// empty list when they do not come.
    virtual void on_drag_wanted() = 0;
};

/// How modifiers travel between a Mac and a PC; between computers of one platform they never
/// change.
enum class ModifierMapping : std::uint8_t {
    Positions = 0, ///< every key as it is: ⌘ is the Windows key
    Shortcuts = 1, ///< Control and Command swapped, so ⌘C copies on a PC (the default)
};

/// The user's input settings on this computer (DEVPLAN Phase 12).
struct InputSettings {
    ModifierMapping modifier_mapping = ModifierMapping::Shortcuts;
    layout::PushConfig push;
    /// Enter modifier bits: the cursor crosses only while one of them is held; 0: always.
    std::uint8_t crossing_modifiers = 0;
};

struct ControlTunables {
    InputSettings settings;
    /// Motion latencies kept for the median and the 99th percentile.
    std::size_t latency_samples = 1000;
    /// An endpoint reports its cursor when something else moved it further than this.
    std::int64_t cursor_report_distance = layout::kUnit;
    /// A push against an edge with a button held asks again this often whether a drag is in
    /// progress, and gives up waiting for an answer after `probe_timeout`.
    Milliseconds probe_interval{200};
    Milliseconds probe_timeout{1000};
    /// Drags dropped on other computers whose items stay available for their transfers; older
    /// ones are withdrawn.
    std::size_t kept_drags = 16;
};

class Control final : public mesh::MeshObserver,
                      public layout::LayoutObserver,
                      public transfer::TransferObserver {
public:
    Control(const DeviceId& self, protocol::Platform platform, const ControlTunables& tunables,
            ControlObserver& observer, layout::LayoutObserver& layout_observer,
            transfer::TransferObserver& transfer_observer, mesh::MeshObserver& next);
    ~Control() override = default;
    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;
    Control(Control&&) = delete;
    Control& operator=(Control&&) = delete;

    /// The mesh to send through, the layout to follow and the transfers that serve this
    /// computer's drags; set once, before the mesh delivers anything.
    void attach(mesh::Mesh& mesh, const layout::Layout& layout,
                transfer::TransferService& transfers);

    // --- Commands, on the mesh's thread; flush the mesh afterwards ---
    /// The members that may control this computer (their control permission is on).
    void set_controllers(std::set<DeviceId> controllers);
    /// The members that may send this computer files: a drag from them may be dropped here.
    void set_file_senders(std::set<DeviceId> senders);
    /// Physical input on this computer, captured at `captured`.
    void input(const Input& input, TimePoint captured);
    /// Brings the cursor home: the user's hotkey (Leave reason 3) or a capture the platform lost
    /// (reason 4).
    void return_home(protocol::LeaveReason reason);
    /// "Lock input here": while locked, the cursor stays on the computer it is on.
    void set_locked(bool locked);
    /// Takes effect at the next crossing; a mapping never changes while keys may be held.
    void set_settings(const InputSettings& settings);
    /// `permille` of this computer's pointer speed while the cursor is on `device`.
    void set_speed(const DeviceId& device, std::uint32_t permille);
    /// Controller: Caps Lock on this computer, while the cursor is home. While it is away, Caps
    /// Lock follows the presses sent to the other computers instead.
    void set_caps_lock(bool on);
    /// Endpoint: this computer cannot be controlled now (its screen is locked, it is going to
    /// sleep); nothing when it can again. A computer being controlled is released.
    void set_unavailable(std::optional<protocol::RefuseReason> reason);
    /// Endpoint: where this computer's cursor really is; reported when something else moved it.
    void cursor_moved(Point position);
    /// Source: a drag began on this computer with these items, or the files of a wanted drag of
    /// promised files arrived; nothing that can travel: an empty list. Its drag ID, 0 without
    /// items. INVALID_ARGUMENT for more than kMaxDragItems items or a relative path.
    [[nodiscard]] Expected<std::uint32_t, Error> drag_started(std::vector<DraggedItem> items);
    /// Source: a drag began on this computer with promised files, which exist only once the drag
    /// is dropped (Photos); they are asked for when the drag is wanted (on_drag_wanted).
    void drag_promised();
    /// Source: this computer's drag ended, however it ended. A drag waiting at the edge for its
    /// outcome goes on: the platform may not see its button held.
    void drag_stopped();
    /// Target: the drag started for an arrival ended.
    void drop_finished(std::uint32_t drag_id, protocol::DragOutcome outcome);

    [[nodiscard]] bool capturing() const { return away_.has_value(); }
    /// The held left button carries a drag.
    [[nodiscard]] bool carrying() const { return carried_.has_value(); }
    [[nodiscard]] std::optional<DeviceId> controller() const;

    // --- layout::LayoutObserver ---
    void on_layout_changed(const layout::LayoutState& state) override;

    // --- transfer::TransferObserver ---
    void on_offer_ready(const transfer::OfferReady& ready) override;
    void on_transfer_progress(const transfer::TransferProgress& progress) override;
    void on_transfer_finished(const transfer::TransferOutcome& outcome) override;
    void on_content_received(const transfer::ContentOutcome& outcome) override;
    void on_send_progress(const transfer::SendProgress& progress) override;

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
        bool drag_source = false; ///< offers its drags
        bool drag_target = false; ///< takes drags from this computer
        protocol::Platform platform = protocol::Platform::MacOS;
    };
    /// The computer this one's input goes to.
    struct Away {
        DeviceId device{};
        std::uint32_t control_id = 0;
        Point sent;            ///< the last position sent
        bool swap_modifiers{}; ///< fixed for the whole entry, so every release matches its press
    };
    /// The controller whose input this computer injects.
    struct Controlled {
        DeviceId controller{};
        std::uint32_t control_id = 0;
        HeldInputs held;
        Point position; ///< where the controller last put the cursor
        bool caps_lock{};
    };

    /// This computer's own drag, as its source.
    struct Outgoing {
        std::uint32_t drag_id = 0;
        std::vector<protocol::OfferedItem> items; ///< each filled in when its scan finishes
        std::size_t scanning = 0;                 ///< items still being scanned
        bool broken = false;                      ///< an item cannot travel: no drag to offer
        std::vector<std::pair<DeviceId, std::uint32_t>> probes; ///< waiting for the scans
        std::optional<DeviceId> offered_to;           ///< the controller it was offered to
        bool parked = false;                          ///< waits at the edge for its outcome
        bool stopped = false;                         ///< the platform reported its end meanwhile
        std::optional<protocol::DragOutcome> outcome; ///< from the computer it went to
        bool promised = false;                        ///< its files come when it is wanted
        bool wanted = false;                          ///< the platform was asked for them
        /// Its files were taken at the edge: the drag on this computer has ended, nothing parks.
        bool detached = false;
    };
    /// The drag the held left button carries, while this computer is the controller.
    struct Carried {
        protocol::DragOffer offer; ///< as forwarded: probe_id 0
    };
    struct Probe {
        DeviceId device{};
        std::uint32_t probe_id = 0;
        TimePoint sent;
    };
    /// A drag started on this computer for an arrival, as a target.
    struct Incoming {
        DeviceId controller{}; ///< this computer when it is the controller, home again
        DeviceId source{};
        std::uint32_t drag_id = 0;
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

    // Drags, as the controller (control_drag.cpp).
    /// A push against `walled` with a button held: asks the computer under the cursor for its
    /// drag.
    void probe(const DeviceId& walled, Point position, TimePoint now);
    void receive_offer(const DeviceId& device, const protocol::DragOffer& offer);
    /// Starts or stops carrying a drag; the edges it may cross change with it.
    void carry(std::optional<Carried> carried);
    /// The cursor came home with the drag it carries.
    void carry_home(protocol::DragOffer offer, Point cursor, CaptureEnd reason);
    [[nodiscard]] bool only_left_held() const;

    // Drags, as the source.
    void receive_probe(const DeviceId& device, const protocol::DragProbe& probe);
    void answer_probes();
    /// Asks the platform for the files of a promised drag, once.
    void want(Outgoing& outgoing);
    [[nodiscard]] protocol::DragOffer outgoing_offer(const Outgoing& outgoing,
                                                     std::uint32_t probe_id) const;
    void receive_result(const DeviceId& device, const protocol::DragResult& result);
    /// Slides a parked drag back; its controller is gone or this computer's own input took over.
    void abandon_parked();
    void withdraw_outgoing();
    void keep_dropped(std::uint32_t drag_id);

    // Drags, as the target.
    /// Starts the drag of a forwarded offer where the controller entered, or lets a parked one go
    /// on.
    void arrive(Controlled& controlled, const protocol::DragOffer& offer);
    /// Ends the drag shown here: `report` sends the source and the controller a cancellation.
    void end_incoming(bool report);

    // Endpoint.
    void receive_enter(const DeviceId& device, const protocol::Enter& enter);
    [[nodiscard]] Controlled* controlled_by(const DeviceId& device, std::uint32_t control_id);
    void inject_key(Controlled& controlled, const protocol::KeyEvent& event);
    void inject_button(Controlled& controlled, const protocol::ButtonEvent& event);
    void receive_motion(Controlled& controlled, const protocol::PointerMotion& motion);
    void take_back();
    /// Releases what is held and ends the control; the controller is told nothing here. Its own
    /// ending: a drag shown here is cancelled. `left`: the controller's Leave, which carries the
    /// drag on; a drag this computer offered it parks.
    void release(bool left = false);
    void release_held(Controlled& controlled);
    void report_latency(const DeviceId& device);

    /// True for the input messages this layer handles.
    bool handle(const DeviceId& device, const protocol::ControlMessage& message);
    void send(const DeviceId& device, protocol::ControlMessage message);

    DeviceId self_;
    ControlTunables tunables_;
    ControlObserver& observer_;
    layout::LayoutObserver& layout_observer_;
    transfer::TransferObserver& transfer_observer_;
    mesh::MeshObserver& next_;
    mesh::Mesh* mesh_ = nullptr;             // set by attach(); outlived by the mesh's owner
    const layout::Layout* layout_ = nullptr; // set by attach()
    transfer::TransferService* transfers_ = nullptr; // set by attach()

    std::map<DeviceId, Peer> peers_; ///< members with an established session
    std::set<DeviceId> controllers_;
    std::set<DeviceId> file_senders_;
    layout::CrossingEngine engine_;
    HeldInputs physical_; ///< this computer's own keys and buttons
    std::optional<Away> away_;
    std::uint32_t last_control_id_ = 0;
    bool locked_ = false;
    protocol::Platform platform_;
    InputSettings settings_;
    bool caps_lock_ = false;

    std::optional<Controlled> controlled_;
    std::optional<protocol::RefuseReason> unavailable_;
    std::map<DeviceId, std::int64_t> clock_offsets_; ///< peer clock minus this one, microseconds
    std::vector<std::int64_t> latencies_;            ///< microseconds, a ring of latency_samples
    std::size_t next_latency_ = 0;
    bool latency_changed_ = false;

    std::optional<Outgoing> outgoing_;
    std::deque<std::uint32_t> dropped_; ///< kept for their transfers, oldest first
    std::uint32_t last_drag_id_ = 0;
    std::optional<Carried> carried_;
    std::optional<Probe> probe_;
    std::optional<TimePoint> last_probe_;
    std::uint32_t last_probe_id_ = 0;
    /// A forwarded offer waiting for its Enter, with the controller that sent it.
    std::optional<std::pair<DeviceId, protocol::DragOffer>> forwarded_;
    std::optional<Incoming> incoming_;
};

} // namespace osdrop::control
