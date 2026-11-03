#pragma once

// The shared arrangement among members (docs/PROTOCOL.md, "Arrangement"). Every member reports
// its own screens to the others and keeps the layout document, which members pass on until all
// have the newest one. A computer missing from the document places itself beside the others.
// The layer sits after the group in the mesh's observer chain, so it hears only from members; it
// keeps ScreenReport and LayoutState to itself and passes everything else on to `next`.

#include "layout/arrangement.hpp"
#include "layout/document.hpp"
#include "layout/layout_state.hpp"
#include "layout/screens.hpp"
#include "mesh/mesh.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"
#include "util/time.hpp"

#include <set>
#include <string_view>

namespace osdrop::layout {

class LayoutObserver {
public:
    virtual ~LayoutObserver() = default;
    /// The arrangement or a member's screens changed: store encode_layout(state) with the
    /// settings.
    virtual void on_layout_changed(const LayoutState& state) = 0;
};

class Layout final : public mesh::MeshObserver {
public:
    Layout(const DeviceId& self, LayoutState state, LayoutObserver& observer,
           mesh::MeshObserver& next);
    ~Layout() override = default;
    Layout(const Layout&) = delete;
    Layout& operator=(const Layout&) = delete;
    Layout(Layout&&) = delete;
    Layout& operator=(Layout&&) = delete;

    /// The mesh to send through; set once, before it delivers anything.
    void attach(mesh::Mesh& mesh);

    [[nodiscard]] const LayoutState& state() const { return state_; }
    /// The computers the cursor can reach among `reachable`, for the crossing engine.
    [[nodiscard]] Arrangement arrangement(const std::set<DeviceId>& reachable) const;

    // --- Commands, on the mesh's thread ---
    /// The group and its members changed. Another group starts from an empty arrangement. May run
    /// inside a mesh callback: what it sends goes out when that callback's entry point returns.
    void group_changed(const protocol::Id& group, std::set<DeviceId> members);
    /// This computer's monitors, at start and after every change.
    [[nodiscard]] Status<Error> set_screens(Screens screens, TimePoint now);
    /// A new arrangement from the editor: members only, and INVALID_LAYOUT when check() finds
    /// problems.
    [[nodiscard]] Status<Error> set_placements(Placements placements, TimePoint now);

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
    void receive_screens(const DeviceId& device, const protocol::ScreenReport& report);
    void receive_document(const DeviceId& device, const protocol::LayoutState& message);
    /// Adds this computer to the document when its screens are known and it is missing.
    void place_self();
    /// A new revision of the document by this computer.
    void publish(Placements placements);
    void send(const DeviceId& device, protocol::ControlMessage message);
    void send_document(std::optional<DeviceId> except);
    void persist();

    DeviceId self_;
    LayoutState state_;
    LayoutObserver& observer_;
    mesh::MeshObserver& next_;
    mesh::Mesh* mesh_ = nullptr; // set by attach(); outlived by the mesh's owner

    std::set<DeviceId> members_;   ///< this computer included
    std::set<DeviceId> connected_; ///< members with an established session
    bool reported_ = false;        ///< set_screens was called in this run
};

} // namespace osdrop::layout
