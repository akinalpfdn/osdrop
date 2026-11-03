#pragma once

// Group membership and trust on top of the mesh (docs/PROTOCOL.md, "Group membership"): who is
// trusted, how a newly paired computer joins, how rosters spread and revocations reach everyone,
// and what each member may do here. The group is the mesh's observer: it answers trust queries
// itself, keeps RosterState to itself, enforces local permissions with specific refusals, and
// passes everything else from members on to the layers above (`next`).

#include "crypto/noise.hpp"
#include "group/group_state.hpp"
#include "mesh/mesh.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace osdrop::group {

using mesh::LogLevel;
using mesh::SessionStats;
using mesh::TrustDecision;
using mesh::TrustVerdict;

/// Group events for the platform, on the mesh's thread.
class GroupObserver {
public:
    virtual ~GroupObserver() = default;
    /// The state to persist changed: store encode_state(state) in the secure store.
    virtual void on_state_changed(const GroupState& state) = 0;
    /// A first pairing: show the code; answer with Mesh::confirm_pairing.
    virtual void on_pairing_code(mesh::ConnectionId connection, std::uint32_t code,
                                 const crypto::Key& peer_key, const protocol::Hello& hello) = 0;
    virtual void on_pairing_failed(mesh::ConnectionId connection, Error reason) = 0;
    /// This computer is no longer in its group: it left (no reason) or a member revoked it.
    virtual void on_left_group(std::optional<Error> reason) = 0;
};

struct GroupConfig {
    protocol::Platform platform = protocol::Platform::MacOS;
    std::string device_name; ///< this computer's roster entry when it creates or is added
};

/// One member as the platform lists it.
struct MemberStatus {
    Member member;
    Permissions permissions;
    bool self = false;
    bool connected = false;
};

class Group final : public mesh::MeshObserver {
public:
    /// `next` receives connection events and admitted messages of members; its on_trust_query
    /// is never called.
    Group(GroupConfig config, const crypto::KeyPair& identity, GroupState state,
          crypto::RandomSource& random, GroupObserver& observer, mesh::MeshObserver& next);
    ~Group() override = default;
    Group(const Group&) = delete;
    Group& operator=(const Group&) = delete;
    Group(Group&&) = delete;
    Group& operator=(Group&&) = delete;

    /// The mesh to act through; set once, before it delivers anything.
    void attach(mesh::Mesh& mesh);

    [[nodiscard]] const GroupState& state() const { return state_; }
    [[nodiscard]] std::vector<MemberStatus> members() const;

    // --- Commands, on the mesh's thread ---
    /// Removes a member from the group everywhere; it is told it was revoked.
    [[nodiscard]] Status<Error> revoke(const DeviceId& device, TimePoint now);
    /// Leaves the group: members learn it, sessions end, the state is cleared.
    [[nodiscard]] Status<Error> leave(TimePoint now);
    [[nodiscard]] Status<Error> set_permissions(const DeviceId& device,
                                                const Permissions& permissions, TimePoint now);

    // --- mesh::MeshObserver ---
    TrustVerdict on_trust_query(const crypto::Key& peer_key, const protocol::Hello& hello) override;
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
    void on_stats(const DeviceId& device, const SessionStats& stats) override;
    void on_log(LogLevel level, std::string_view message) override;

private:
    struct Joining {
        crypto::Key key{};
        protocol::Hello hello;
    };
    struct Session {
        crypto::Key key{};
        protocol::Hello hello;
        protocol::Capabilities capabilities;
    };

    void receive_roster(const DeviceId& device, const protocol::RosterState& message);
    void adopt(const DeviceId& device, const protocol::RosterState& message);
    /// After any roster change: persist, tell connected members, act on what it means.
    void roster_changed(std::optional<DeviceId> from);
    void send_roster(const DeviceId& device);
    void create_group_with(const DeviceId& device, const Joining& joining);
    void add_member(const DeviceId& device, const Joining& joining);
    void announce(const DeviceId& device);
    void clear(std::optional<Error> reason, protocol::ByeReason bye);
    [[nodiscard]] Member self_member(std::uint32_t epoch) const;
    [[nodiscard]] bool admits(const DeviceId& device, const protocol::ControlMessage& message);
    void persist();
    void log(LogLevel level, std::string_view message);

    GroupConfig config_;
    crypto::Key public_key_{};
    DeviceId self_{};
    GroupState state_;
    crypto::RandomSource& random_;
    GroupObserver& observer_;
    mesh::MeshObserver& next_;
    mesh::Mesh* mesh_ = nullptr; // set by attach(); outlived by the mesh's owner

    std::map<DeviceId, Joining> joining_;  ///< paired in this run, not yet members on both sides
    std::map<DeviceId, Session> sessions_; ///< established sessions
    std::set<DeviceId> announced_;         ///< members whose connection `next` was told about
    std::map<mesh::ConnectionId, DeviceId> pairing_prompts_; ///< codes shown, not answered yet
};

} // namespace osdrop::group
