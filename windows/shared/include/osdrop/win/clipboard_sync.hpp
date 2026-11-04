#pragma once

// The shared clipboard on the PC (DEVPLAN, Phase 15), free of Windows headers. A copy on this PC
// is reported to core while a member is connected; a copy on another computer is fetched in the
// background within limits and written to the clipboard as real data once everything arrived, so
// a paste never waits on the network (user decision, as on the Mac). The Win32 clipboard sits
// behind ClipboardHost (windows/engine). All of the sync's work runs on the host's clipboard
// thread: core's callbacks only post to it, so results are always handled after their request
// returned.

#include <osdrop/osdrop.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace osdrop::win {

using ClipDevice = std::array<std::uint8_t, OSDROP_DEVICE_ID_SIZE>;

struct ClipContent {
    osdrop_clipboard_format format = OSDROP_CLIPBOARD_TEXT;
    std::vector<std::uint8_t> bytes{};
    bool operator==(const ClipContent&) const = default;
};

/// A copy in the shared formats: UTF-8 text, RTF, plain HTML (UTF-8), PNG, and files.
struct ClipData {
    std::vector<ClipContent> contents{};
    std::vector<std::string> files{}; ///< absolute UTF-8 paths
    bool operator==(const ClipData&) const = default;
};

/// The largest copy of each kind moved through the clipboard.
struct ClipboardLimits {
    std::uint64_t text = std::uint64_t{16} * 1024 * 1024;
    std::uint64_t image = std::uint64_t{64} * 1024 * 1024;
    std::uint64_t files = std::uint64_t{512} * 1024 * 1024; ///< user decision, Phase 15

    [[nodiscard]] std::uint64_t limit(osdrop_clipboard_format format) const;
};

/// Why another computer's copy is not on the clipboard.
enum class ClipboardNotice : std::uint8_t {
    Unavailable, ///< nothing of it could be fetched
    TooLarge,    ///< larger than the limits; drag and drop moves it
};

/// The Win32 clipboard, as the sync uses it.
class ClipboardHost {
public:
    virtual ~ClipboardHost() = default;
    /// Runs `work` on the clipboard thread after what was posted before; dropped once stopped.
    virtual void post(std::function<void()> work) = 0;
    /// Writes another computer's copy to the clipboard. On the clipboard thread.
    virtual void write(const ClipData& data) = 0;
    /// A new, empty folder for one copy's files (UTF-8); nullopt when none can be made.
    virtual std::optional<std::string> make_staging_folder() = 0;
    /// Removes every staging folder but `kept`.
    virtual void clear_staging(const std::optional<std::string>& kept) = 0;
};

/// What the sync asks of core; the real one calls the C API.
class ClipboardMesh {
public:
    virtual ~ClipboardMesh() = default;
    virtual osdrop_result copied(const ClipData& data) = 0;
    virtual osdrop_result fetch_content(const ClipDevice& device, const osdrop_object_ref& item,
                                        std::uint64_t max_bytes, std::uint64_t& id) = 0;
    virtual osdrop_result receive(const ClipDevice& device, const osdrop_object_ref& item,
                                  const std::string& folder, std::uint64_t max_bytes,
                                  std::uint64_t& id) = 0;
};

/// ClipboardMesh over the C API.
class CoreClipboardMesh final : public ClipboardMesh {
public:
    explicit CoreClipboardMesh(osdrop_mesh* mesh) : mesh_(mesh) {}
    osdrop_result copied(const ClipData& data) override;
    osdrop_result fetch_content(const ClipDevice& device, const osdrop_object_ref& item,
                                std::uint64_t max_bytes, std::uint64_t& id) override;
    osdrop_result receive(const ClipDevice& device, const osdrop_object_ref& item,
                          const std::string& folder, std::uint64_t max_bytes,
                          std::uint64_t& id) override;

private:
    osdrop_mesh* mesh_;
};

class ClipboardSync {
public:
    using Notify = std::function<void(ClipboardNotice notice, const ClipDevice& device,
                                      const std::string& name)>;

    ClipboardSync(ClipboardHost& host, Notify notify, ClipboardLimits limits = {});
    ~ClipboardSync() = default;
    ClipboardSync(const ClipboardSync&) = delete;
    ClipboardSync& operator=(const ClipboardSync&) = delete;
    ClipboardSync(ClipboardSync&&) = delete;
    ClipboardSync& operator=(ClipboardSync&&) = delete;

    /// The mesh to report and fetch through, set on the clipboard thread; what core reports
    /// before it is ignored.
    void attach(ClipboardMesh& mesh);

    // --- On the clipboard thread ---
    /// A copy on this PC, already checked for concealed data.
    void copied(ClipData data);

    // --- From core's callbacks: they post to the clipboard thread ---
    void peer_connected(const ClipDevice& device, std::string name);
    void peer_disconnected(const ClipDevice& device);
    void clipboard_changed(const ClipDevice& device, std::uint64_t clip_id,
                           std::vector<osdrop_announced_format> formats);
    void content_received(std::uint64_t id, osdrop_result result,
                          std::vector<std::uint8_t> content);
    void transfer_finished(std::uint64_t id, osdrop_result result, std::vector<std::string> items);

private:
    struct Incoming {
        ClipDevice device{};
        std::uint64_t clip_id = 0;
        std::size_t requests = 0;
        std::size_t settled = 0;
        std::map<std::uint64_t, osdrop_clipboard_format> pending{};
        ClipData received{};
        std::optional<std::string> folder{};
        bool too_large = false;
    };

    void begin(const ClipDevice& device, std::uint64_t clip_id,
               const std::vector<osdrop_announced_format>& formats);
    void request(osdrop_clipboard_format format);
    void settle(std::uint64_t id, osdrop_result result, std::vector<std::uint8_t> content,
                std::vector<std::string> items);
    void finish();
    [[nodiscard]] std::string name_of(const ClipDevice& device) const;

    ClipboardHost& host_;
    Notify notify_;
    ClipboardLimits limits_;
    ClipboardMesh* mesh_ = nullptr;
    // On the clipboard thread only:
    std::map<ClipDevice, std::string> connected_;
    std::optional<Incoming> incoming_;
};

} // namespace osdrop::win
