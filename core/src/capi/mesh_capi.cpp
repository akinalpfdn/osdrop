#include "capi/random_source.hpp"
#include "clipboard/clipboard.hpp"
#include "control/control.hpp"
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
#include "transfer/file_thread.hpp"
#include "transfer/files.hpp"
#include "transfer/service.hpp"
#include "transport/asio_node.hpp"

#include <osdrop/osdrop.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
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
namespace clipboard = osdrop::clipboard;
namespace control = osdrop::control;
namespace layout = osdrop::layout;
namespace mesh = osdrop::mesh;
namespace protocol = osdrop::protocol;
namespace transfer = osdrop::transfer;
using osdrop::Error;
using osdrop::TimePoint;

static_assert(OSDROP_LOG_DEBUG == static_cast<int>(mesh::LogLevel::Debug) &&
              OSDROP_LOG_ERROR == static_cast<int>(mesh::LogLevel::Error));
static_assert(OSDROP_LAYOUT_OVERLAP == static_cast<int>(layout::ProblemKind::Overlap) &&
              OSDROP_LAYOUT_UNREACHABLE == static_cast<int>(layout::ProblemKind::Unreachable));
static_assert(OSDROP_FEATURE_TAKES_CONTROL == protocol::kFeatureTakesControl &&
              OSDROP_FEATURE_DRAG_SOURCE == protocol::kFeatureDragSource &&
              OSDROP_FEATURE_DRAG_TARGET == protocol::kFeatureDragTarget &&
              OSDROP_FEATURE_CLIPBOARD == protocol::kFeatureClipboard &&
              OSDROP_FEATURE_FILES == protocol::kFeatureFiles);
static_assert(OSDROP_MONITOR_KEY_SIZE == protocol::kIdSize &&
              OSDROP_MAX_MONITORS == protocol::kMaxMonitors);
static_assert(OSDROP_OBJECT_DRAG == static_cast<int>(protocol::ObjectKind::Drag) &&
              OSDROP_OBJECT_CLIPBOARD == static_cast<int>(protocol::ObjectKind::Clipboard));
static_assert(OSDROP_CLIPBOARD_TEXT == static_cast<int>(protocol::ClipboardFormat::PlainText) &&
              OSDROP_CLIPBOARD_RTF == static_cast<int>(protocol::ClipboardFormat::RichText) &&
              OSDROP_CLIPBOARD_HTML == static_cast<int>(protocol::ClipboardFormat::Html) &&
              OSDROP_CLIPBOARD_IMAGE == static_cast<int>(protocol::ClipboardFormat::Image) &&
              OSDROP_CLIPBOARD_FILES == static_cast<int>(protocol::ClipboardFormat::Files));

constexpr std::uint8_t kMaxButton = 31;
constexpr std::uint8_t kMaxScrollPhase = 5;
constexpr std::uint8_t kMaxMomentumPhase = 3;

osdrop_result to_result(Error error) {
    return static_cast<osdrop_result>(error);
}

/// Physical input from the platform, checked.
osdrop::Expected<control::Input, Error> to_physical_input(const osdrop_input& input) {
    using Result = osdrop::Expected<control::Input, Error>;
    switch (input.kind) {
    case OSDROP_INPUT_POINTER:
        return Result(control::PointerMoved{.position = {.x = input.x, .y = input.y},
                                            .delta = {.x = input.dx, .y = input.dy}});
    case OSDROP_INPUT_KEY:
        if ((input.page != protocol::kKeyboardPage && input.page != protocol::kConsumerPage) ||
            input.usage == 0 || input.action < 1 || input.action > 3) {
            return Result::failure(Error::InvalidArgument);
        }
        return Result(control::KeyInput{.key = {.page = input.page, .usage = input.usage},
                                        .action = static_cast<protocol::KeyAction>(input.action)});
    case OSDROP_INPUT_BUTTON:
        if (input.button > kMaxButton || input.action < 1 || input.action > 2 ||
            input.click_count == 0) {
            return Result::failure(Error::InvalidArgument);
        }
        return Result(
            control::ButtonInput{.button = input.button,
                                 .action = static_cast<protocol::ButtonAction>(input.action),
                                 .click_count = input.click_count});
    case OSDROP_INPUT_SCROLL:
        if (input.scroll_unit < 1 || input.scroll_unit > 2 ||
            input.scroll_phase > kMaxScrollPhase || input.momentum > kMaxMomentumPhase) {
            return Result::failure(Error::InvalidArgument);
        }
        return Result(
            control::ScrollInput{.unit = static_cast<protocol::ScrollUnit>(input.scroll_unit),
                                 .phase = static_cast<protocol::ScrollPhase>(input.scroll_phase),
                                 .momentum = static_cast<protocol::MomentumPhase>(input.momentum),
                                 .dx = input.dx,
                                 .dy = input.dy});
    default:
        return Result::failure(Error::InvalidArgument);
    }
}

/// An event for the platform to inject, as the C API carries it.
osdrop_input to_input(const control::Injection& injection) {
    osdrop_input input{.kind = OSDROP_INPUT_POINTER,
                       .x = 0,
                       .y = 0,
                       .dx = 0,
                       .dy = 0,
                       .page = 0,
                       .usage = 0,
                       .action = 0,
                       .button = 0,
                       .click_count = 0,
                       .scroll_unit = 0,
                       .scroll_phase = 0,
                       .momentum = 0};
    if (const auto* move = std::get_if<control::MoveCursor>(&injection)) {
        input.kind = OSDROP_INPUT_POINTER;
        input.x = static_cast<std::int32_t>(move->position.x);
        input.y = static_cast<std::int32_t>(move->position.y);
    } else if (const auto* key = std::get_if<protocol::KeyEvent>(&injection)) {
        input.kind = OSDROP_INPUT_KEY;
        input.page = key->page;
        input.usage = key->usage;
        input.action = static_cast<std::uint8_t>(key->action);
    } else if (const auto* button = std::get_if<protocol::ButtonEvent>(&injection)) {
        input.kind = OSDROP_INPUT_BUTTON;
        input.x = button->x;
        input.y = button->y;
        input.action = static_cast<std::uint8_t>(button->action);
        input.button = button->button;
        input.click_count = button->click_count;
    } else if (const auto* scroll = std::get_if<protocol::ScrollEvent>(&injection)) {
        input.kind = OSDROP_INPUT_SCROLL;
        input.x = scroll->x;
        input.y = scroll->y;
        input.dx = scroll->dx;
        input.dy = scroll->dy;
        input.scroll_unit = static_cast<std::uint8_t>(scroll->unit);
        input.scroll_phase = static_cast<std::uint8_t>(scroll->phase);
        input.momentum = static_cast<std::uint8_t>(scroll->momentum);
    }
    return input;
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

osdrop_object_ref to_c(const protocol::ObjectRef& item) {
    return {.kind = static_cast<osdrop_object_kind>(item.kind),
            .object_id = item.object_id,
            .item_index = item.item_index};
}

/// An object reference the protocol can carry (PROTOCOL.md, "Transfer streams").
osdrop::Expected<protocol::ObjectRef, Error> to_object(const osdrop_object_ref& item) {
    using Result = osdrop::Expected<protocol::ObjectRef, Error>;
    if (item.kind != OSDROP_OBJECT_DRAG && item.kind != OSDROP_OBJECT_CLIPBOARD) {
        return Result::failure(Error::ValueOutOfRange);
    }
    const bool drag = item.kind == OSDROP_OBJECT_DRAG;
    if (item.object_id == 0 || (drag && item.item_index >= protocol::kMaxDragItems) ||
        (!drag && (item.item_index < 1 || item.item_index > protocol::kMaxClipboardFormats))) {
        return Result::failure(Error::ValueOutOfRange);
    }
    return protocol::ObjectRef{.kind = static_cast<protocol::ObjectKind>(item.kind),
                               .object_id = item.object_id,
                               .item_index = item.item_index};
}

/// Turns group, layout and transfer events, and the mesh events passed on for members, into the
/// platform's C callbacks, and tells the layout and the transfers about group changes. The group
/// answers trust and handles pairing itself, so those mesh events never arrive here.
class CallbackObserver final : public group::GroupObserver,
                               public layout::LayoutObserver,
                               public transfer::TransferObserver,
                               public clipboard::ClipboardObserver,
                               public control::ControlObserver,
                               public mesh::MeshObserver {
public:
    explicit CallbackObserver(const osdrop_mesh_callbacks& callbacks) : callbacks_(callbacks) {}

    /// The layers to tell about group changes; set once, before the mesh starts.
    void follow(layout::Layout& arrangement, transfer::TransferService& transfers,
                clipboard::Clipboard& shared, control::Control& input) {
        layout_ = &arrangement;
        transfers_ = &transfers;
        clipboard_ = &shared;
        control_ = &input;
    }

    // --- group::GroupObserver ---
    void on_state_changed(const group::GroupState& state) override {
        if (layout_ != nullptr) {
            layout_->group_changed(state.group_id, members_of(state));
        }
        if (transfers_ != nullptr) {
            transfers_->set_files_permitted(state.file_senders());
        }
        if (clipboard_ != nullptr) {
            clipboard_->set_members(state.clipboard_members());
        }
        if (control_ != nullptr) {
            control_->set_controllers(state.controllers());
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

    // --- transfer::TransferObserver ---
    void on_offer_ready(const transfer::OfferReady& ready) override {
        if (callbacks_.offer_ready != nullptr) {
            const osdrop_object_ref item = to_c(ready.item);
            callbacks_.offer_ready(callbacks_.context, &item,
                                   ready.error ? to_result(*ready.error) : OSDROP_OK,
                                   ready.total_bytes, ready.entry_count, ready.skipped);
        }
    }
    void on_transfer_progress(const transfer::TransferProgress& progress) override {
        if (callbacks_.transfer_progress != nullptr) {
            const osdrop_transfer_progress c_progress{.transfer_id = progress.id,
                                                      .bytes_done = progress.bytes_done,
                                                      .bytes_total = progress.bytes_total,
                                                      .files_done = progress.files_done,
                                                      .files_total = progress.files_total};
            callbacks_.transfer_progress(callbacks_.context, &c_progress);
        }
    }
    void on_transfer_finished(const transfer::TransferOutcome& outcome) override {
        if (callbacks_.transfer_finished == nullptr) {
            return;
        }
        std::vector<std::string> paths;
        paths.reserve(outcome.items.size());
        for (const std::filesystem::path& item : outcome.items) {
            paths.push_back(transfer::utf8_of(item));
        }
        std::vector<const char*> items;
        items.reserve(paths.size());
        for (const std::string& path : paths) {
            items.push_back(path.c_str());
        }
        callbacks_.transfer_finished(callbacks_.context, outcome.id,
                                     outcome.error ? to_result(*outcome.error) : OSDROP_OK,
                                     items.data(), items.size(), outcome.skipped_links);
    }
    void on_content_received(const transfer::ContentOutcome& outcome) override {
        if (callbacks_.content_received != nullptr) {
            callbacks_.content_received(callbacks_.context, outcome.id,
                                        outcome.error ? to_result(*outcome.error) : OSDROP_OK,
                                        outcome.content.data(), outcome.content.size());
        }
    }

    // --- clipboard::ClipboardObserver ---
    void on_remote_clip(const clipboard::RemoteClip& clip) override {
        if (callbacks_.clipboard_changed == nullptr) {
            return;
        }
        std::vector<osdrop_announced_format> formats;
        formats.reserve(clip.formats.size());
        for (const protocol::AnnouncedFormat& format : clip.formats) {
            formats.push_back({.format = static_cast<osdrop_clipboard_format>(format.format),
                               .byte_count = format.byte_count});
        }
        callbacks_.clipboard_changed(callbacks_.context, clip.device.data(), clip.clip_id,
                                     formats.data(), formats.size());
    }

    // --- control::ControlObserver ---
    void on_capture_began(const mesh::DeviceId& device,
                          std::span<const control::HeldKey> release_here) override {
        if (callbacks_.capture_began == nullptr) {
            return;
        }
        std::vector<osdrop_input> keys;
        keys.reserve(release_here.size());
        for (const control::HeldKey& key : release_here) {
            keys.push_back(to_input(protocol::KeyEvent{.control_id = 0,
                                                       .page = key.page,
                                                       .usage = key.usage,
                                                       .action = protocol::KeyAction::Up}));
        }
        callbacks_.capture_began(callbacks_.context, device.data(), keys.data(), keys.size());
    }
    void on_capture_ended(const control::CaptureEnded& ended) override {
        if (callbacks_.capture_ended != nullptr) {
            callbacks_.capture_ended(
                callbacks_.context, ended.device.data(), static_cast<std::int32_t>(ended.cursor.x),
                static_cast<std::int32_t>(ended.cursor.y), static_cast<std::uint8_t>(ended.reason),
                ended.refused ? static_cast<std::uint8_t>(*ended.refused) : 0);
        }
    }
    void on_controlled(const std::optional<mesh::DeviceId>& controller) override {
        if (callbacks_.controlled != nullptr) {
            callbacks_.controlled(callbacks_.context, controller ? controller->data() : nullptr);
        }
    }
    void on_inject(const control::Injection& injection) override {
        if (callbacks_.inject != nullptr) {
            const osdrop_input input = to_input(injection);
            callbacks_.inject(callbacks_.context, &input);
        }
    }
    void on_input_latency(const mesh::DeviceId& controller,
                          const control::LatencyStats& stats) override {
        if (callbacks_.input_latency != nullptr) {
            callbacks_.input_latency(callbacks_.context, controller.data(),
                                     static_cast<std::uint32_t>(stats.samples),
                                     static_cast<std::uint32_t>(stats.median.count()),
                                     static_cast<std::uint32_t>(stats.p99.count()));
        }
    }

private:
    osdrop_mesh_callbacks callbacks_;
    layout::Layout* layout_ = nullptr;
    transfer::TransferService* transfers_ = nullptr;
    clipboard::Clipboard* clipboard_ = nullptr;
    control::Control* control_ = nullptr;
};

/// File work on the file thread; results back on the node's I/O thread, with the mesh flushed.
class NodeExecutor final : public transfer::TransferExecutor {
public:
    explicit NodeExecutor(transfer::FileThread& files) : files_(&files) {}

    /// Set once, right after the node starts; nothing posts results before.
    void run_with(osdrop::transport::AsioNode& node) { node_ = &node; }

    void run_file_work(std::function<void()> work) override { files_->post(std::move(work)); }
    void run_on_mesh(std::function<void(TimePoint)> work) override {
        node_->run([moved = std::move(work)](mesh::Mesh& mesh, TimePoint now) {
            moved(now);
            mesh.flush(now);
        });
    }

private:
    transfer::FileThread* files_;
    osdrop::transport::AsioNode* node_ = nullptr;
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
        if (p.detached > 1 || !result
                                   .emplace(read_id(std::data(p.device_id)),
                                            layout::Placement{.offset = {.x = p.x, .y = p.y},
                                                              .detached = p.detached == 1})
                                   .second) {
            return Result::failure(Error::InvalidArgument);
        }
    }
    return result;
}

} // namespace

/// The C handle: the platform's callbacks, the group, the layout, the transfers and the node that
/// runs the mesh. The layers live on the I/O thread: every access goes through the node.
struct osdrop_mesh {
    osdrop_mesh(const osdrop_random_source& random_source, const osdrop_mesh_callbacks& callbacks)
        : random(random_source), observer(callbacks), executor(file_thread) {}
    ~osdrop_mesh() {
        try {
            // Transfers in progress are rolled back, then the file thread finishes what is queued
            // (those rollbacks included) while the node still takes their results.
            if (node && transfers) {
                static_cast<void>(node->run_and_wait([this](mesh::Mesh& mesh, TimePoint now) {
                    transfers->shutdown();
                    mesh.flush(now);
                }));
            }
            file_thread.stop();
        } catch (...) {
            std::terminate(); // only locking and joining can throw: unrecoverable
        }
    }
    osdrop_mesh(const osdrop_mesh&) = delete;
    osdrop_mesh& operator=(const osdrop_mesh&) = delete;
    osdrop_mesh(osdrop_mesh&&) = delete;
    osdrop_mesh& operator=(osdrop_mesh&&) = delete;

    osdrop::capi::CallbackRandomSource random;
    CallbackObserver observer;
    transfer::FileThread file_thread;
    NodeExecutor executor;
    std::unique_ptr<control::Control> control;
    std::unique_ptr<clipboard::Clipboard> clipboard;
    std::unique_ptr<transfer::TransferService> transfers;
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
    // mesh → group → layout → transfers → clipboard → control → callbacks; the clipboard also
    // stands between the transfers and the callbacks, the control between the layout and them.
    const mesh::DeviceId self = osdrop::crypto::device_id(identity.value().public_key());
    handle->control = std::make_unique<control::Control>(
        self, control::ControlTunables{}, handle->observer, handle->observer, handle->observer);
    handle->clipboard = std::make_unique<clipboard::Clipboard>(self, handle->observer,
                                                               handle->observer, *handle->control);
    handle->transfers = std::make_unique<transfer::TransferService>(
        transfer::TransferTunables{.max_streams = config->max_transfer_streams}, handle->executor,
        *handle->clipboard, *handle->clipboard);
    handle->layout = std::make_unique<layout::Layout>(self, std::move(arrangement.value()),
                                                      *handle->control, *handle->transfers);
    handle->observer.follow(*handle->layout, *handle->transfers, *handle->clipboard,
                            *handle->control);
    handle->layout->group_changed(state.value().group_id, members_of(state.value()));
    handle->transfers->set_files_permitted(state.value().file_senders());
    handle->clipboard->set_members(state.value().clipboard_members());
    handle->control->set_controllers(state.value().controllers());
    handle->group = std::make_unique<group::Group>(
        group::GroupConfig{.platform = mesh_config.platform, .device_name = name}, identity.value(),
        std::move(state.value()), handle->random, handle->observer, *handle->layout);
    group::Group& member_layer = *handle->group;
    layout::Layout& arrangement_layer = *handle->layout;
    transfer::TransferService& transfer_layer = *handle->transfers;
    clipboard::Clipboard& clipboard_layer = *handle->clipboard;
    control::Control& control_layer = *handle->control;
    auto node = osdrop::transport::AsioNode::create(
        std::move(mesh_config), std::move(identity.value()), handle->random, member_layer,
        osdrop::transport::NodeOptions{.listen_port = config->listen_port},
        [&member_layer, &arrangement_layer, &transfer_layer, &clipboard_layer,
         &control_layer](mesh::Mesh& mesh) {
            member_layer.attach(mesh);
            arrangement_layer.attach(mesh);
            transfer_layer.attach(mesh);
            clipboard_layer.attach(mesh, transfer_layer);
            control_layer.attach(mesh, arrangement_layer);
        });
    if (!node) {
        return to_result(node.error());
    }
    handle->node = std::move(node.value());
    handle->executor.run_with(*handle->node);
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

osdrop_result osdrop_mesh_offer(osdrop_mesh* mesh, const osdrop_object_ref* item,
                                const char* const* paths, size_t path_count,
                                const uint8_t* audience, size_t audience_count) {
    if (mesh == nullptr || item == nullptr || paths == nullptr || path_count == 0 ||
        (audience == nullptr && audience_count != 0)) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const auto object = to_object(*item);
    if (!object) {
        return to_result(object.error());
    }
    std::vector<std::filesystem::path> roots;
    roots.reserve(path_count);
    for (const char* path : std::span(paths, path_count)) {
        if (path == nullptr) {
            return OSDROP_ERROR_INVALID_ARGUMENT;
        }
        roots.push_back(transfer::path_from_utf8(path));
    }
    std::set<mesh::DeviceId> devices;
    for (std::size_t i = 0; i < audience_count; ++i) {
        devices.insert(read_id(std::span(audience, audience_count * protocol::kIdSize)
                                   .subspan(i * protocol::kIdSize)
                                   .data()));
    }
    auto result = osdrop::Status<Error>::failure(Error::Internal);
    transfer::TransferService& transfers = *mesh->transfers;
    static_cast<void>(mesh->node->run_and_wait([&](mesh::Mesh& /*mesh*/, TimePoint /*now*/) {
        result = transfers.offer(object.value(), std::move(roots), std::move(devices));
    }));
    return to_result(result);
}

osdrop_result osdrop_mesh_withdraw(osdrop_mesh* mesh, osdrop_object_kind kind, uint64_t object_id) {
    if (mesh == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    if ((kind != OSDROP_OBJECT_DRAG && kind != OSDROP_OBJECT_CLIPBOARD) || object_id == 0) {
        return OSDROP_ERROR_VALUE_OUT_OF_RANGE;
    }
    transfer::TransferService& transfers = *mesh->transfers;
    const bool ran = mesh->node->run_and_wait([&](mesh::Mesh& node_mesh, TimePoint now) {
        transfers.withdraw(static_cast<protocol::ObjectKind>(kind), object_id);
        node_mesh.flush(now);
    });
    return ran ? OSDROP_OK : OSDROP_ERROR_INTERNAL;
}

osdrop_result osdrop_mesh_receive(osdrop_mesh* mesh, const uint8_t* device_id,
                                  const osdrop_object_ref* item, const char* destination,
                                  uint64_t max_bytes, uint64_t* out_transfer_id) {
    if (mesh == nullptr || device_id == nullptr || item == nullptr || destination == nullptr ||
        out_transfer_id == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const auto object = to_object(*item);
    if (!object) {
        return to_result(object.error());
    }
    const protocol::Id device = read_id(device_id);
    const std::filesystem::path folder = transfer::path_from_utf8(destination);
    auto result = osdrop::Expected<transfer::TransferId, Error>::failure(Error::Internal);
    transfer::TransferService& transfers = *mesh->transfers;
    static_cast<void>(mesh->node->run_and_wait([&](mesh::Mesh& node_mesh, TimePoint now) {
        result = transfers.receive(device, object.value(), folder, max_bytes);
        node_mesh.flush(now);
    }));
    if (!result) {
        return to_result(result.error());
    }
    *out_transfer_id = result.value();
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_fetch_content(osdrop_mesh* mesh, const uint8_t* device_id,
                                        const osdrop_object_ref* item, uint64_t max_bytes,
                                        uint64_t* out_transfer_id) {
    if (mesh == nullptr || device_id == nullptr || item == nullptr || out_transfer_id == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const auto object = to_object(*item);
    if (!object) {
        return to_result(object.error());
    }
    const protocol::Id device = read_id(device_id);
    auto result = osdrop::Expected<transfer::TransferId, Error>::failure(Error::Internal);
    transfer::TransferService& transfers = *mesh->transfers;
    static_cast<void>(mesh->node->run_and_wait([&](mesh::Mesh& node_mesh, TimePoint now) {
        result = transfers.fetch_content(device, object.value(), max_bytes);
        node_mesh.flush(now);
    }));
    if (!result) {
        return to_result(result.error());
    }
    *out_transfer_id = result.value();
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_clipboard_copied(osdrop_mesh* mesh,
                                           const osdrop_clipboard_content* contents, size_t count,
                                           const char* const* files, size_t file_count,
                                           uint64_t* out_clip_id) {
    if (mesh == nullptr || out_clip_id == nullptr || (contents == nullptr && count != 0) ||
        (files == nullptr && file_count != 0) || count > protocol::kMaxClipboardFormats) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    std::vector<clipboard::LocalFormat> formats;
    formats.reserve(count);
    for (const osdrop_clipboard_content& content : std::span(contents, count)) {
        if ((content.data == nullptr && content.size != 0) ||
            content.format < OSDROP_CLIPBOARD_TEXT || content.format > OSDROP_CLIPBOARD_IMAGE) {
            return OSDROP_ERROR_INVALID_ARGUMENT;
        }
        const auto bytes = std::span(content.data, content.size);
        formats.push_back({.format = static_cast<protocol::ClipboardFormat>(content.format),
                           .bytes = {bytes.begin(), bytes.end()}});
    }
    std::vector<std::filesystem::path> paths;
    paths.reserve(file_count);
    for (const char* file : std::span(files, file_count)) {
        if (file == nullptr) {
            return OSDROP_ERROR_INVALID_ARGUMENT;
        }
        paths.push_back(transfer::path_from_utf8(file));
    }
    const auto wall_us = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    auto result = osdrop::Expected<std::uint64_t, Error>::failure(Error::Internal);
    clipboard::Clipboard& shared = *mesh->clipboard;
    static_cast<void>(mesh->node->run_and_wait([&](mesh::Mesh& node_mesh, TimePoint now) {
        result = shared.copied(std::move(formats), std::move(paths), wall_us);
        node_mesh.flush(now);
    }));
    if (!result) {
        return to_result(result.error());
    }
    *out_clip_id = result.value();
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_input(osdrop_mesh* mesh, const osdrop_input* input, uint64_t age_us) {
    if (mesh == nullptr || input == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    auto event = to_physical_input(*input);
    if (!event) {
        return to_result(event.error());
    }
    const TimePoint captured = osdrop::Clock::now() - osdrop::Microseconds(age_us);
    control::Control& input_layer = *mesh->control;
    mesh->node->run(
        [&input_layer, event = event.value(), captured](mesh::Mesh& node_mesh, TimePoint now) {
            input_layer.input(event, captured);
            node_mesh.flush(now);
        });
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_return_home(osdrop_mesh* mesh, uint8_t input_lost) {
    if (mesh == nullptr || input_lost > 1) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const auto reason = input_lost == 1 ? protocol::LeaveReason::ControllerLost
                                        : protocol::LeaveReason::ReleasedByHotkey;
    control::Control& input_layer = *mesh->control;
    mesh->node->run([&input_layer, reason](mesh::Mesh& node_mesh, TimePoint now) {
        input_layer.return_home(reason);
        node_mesh.flush(now);
    });
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_lock_input(osdrop_mesh* mesh, uint8_t locked) {
    if (mesh == nullptr || locked > 1) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    control::Control& input_layer = *mesh->control;
    mesh->node->run([&input_layer, locked](mesh::Mesh& node_mesh, TimePoint now) {
        input_layer.set_locked(locked == 1);
        node_mesh.flush(now);
    });
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_set_unavailable(osdrop_mesh* mesh, uint8_t reason) {
    if (mesh == nullptr ||
        reason > static_cast<std::uint8_t>(protocol::RefuseReason::NotPermitted)) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    const std::optional<protocol::RefuseReason> refused =
        reason == 0 ? std::nullopt : std::optional(static_cast<protocol::RefuseReason>(reason));
    control::Control& input_layer = *mesh->control;
    mesh->node->run([&input_layer, refused](mesh::Mesh& node_mesh, TimePoint now) {
        input_layer.set_unavailable(refused);
        node_mesh.flush(now);
    });
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_cursor_moved(osdrop_mesh* mesh, int32_t x, int32_t y) {
    if (mesh == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    control::Control& input_layer = *mesh->control;
    mesh->node->run([&input_layer, x, y](mesh::Mesh& node_mesh, TimePoint now) {
        input_layer.cursor_moved({.x = x, .y = y});
        node_mesh.flush(now);
    });
    return OSDROP_OK;
}

osdrop_result osdrop_mesh_cancel_transfer(osdrop_mesh* mesh, uint64_t transfer_id) {
    if (mesh == nullptr) {
        return OSDROP_ERROR_INVALID_ARGUMENT;
    }
    transfer::TransferService& transfers = *mesh->transfers;
    const bool ran = mesh->node->run_and_wait([&](mesh::Mesh& node_mesh, TimePoint now) {
        transfers.cancel(transfer_id);
        node_mesh.flush(now);
    });
    return ran ? OSDROP_OK : OSDROP_ERROR_INTERNAL;
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
        auto monitors = to_screens(std::data(computer.monitors), computer.monitor_count);
        if (!monitors) {
            return to_result(monitors.error());
        }
        const layout::DeviceId device = read_id(std::data(computer.placement.device_id));
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
