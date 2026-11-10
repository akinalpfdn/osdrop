#pragma once

// A drag that came from another computer, as the PC's drag starter reads it (DEVPLAN Phase 14).
// Every item is received into a folder of its own under the drag's staging folder as soon as the
// drag arrives, so a drop does not wait for its first request to cross the network. Core plans an
// item's entries before any of their content (transfer_planned) and reports each file as it is
// written (entry_written): the tree is known once every item is planned, and a file can be read
// while it arrives, so Explorer copies it with its own progress as it comes. A file's last byte
// is held back until core verified it, so a drop never completes a file that failed its check.
// Everything here is free of Windows headers, so it is unit tested and linted on every host.

#include <osdrop/osdrop.h>
#include <osdrop/win/drag_files.hpp>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace osdrop::win::drag {

using SourceId = std::array<std::uint8_t, OSDROP_DEVICE_ID_SIZE>;

/// An entry of a received item as core planned it (osdrop_received_entry).
struct PlannedEntry {
    std::string path;      ///< '/' between names, the item's own name first
    std::uint8_t kind = 1; ///< 1 file, 2 folder, 3 link
    std::uint64_t size = 0;
    std::int64_t modified_us = 0; ///< since 1970 UTC
};

/// What core reports about one transfer, routed to the drag that started it.
class TransferSink {
public:
    virtual ~TransferSink() = default;
    virtual void planned(std::uint64_t transfer_id, const std::vector<PlannedEntry>& entries) = 0;
    virtual void written(std::uint64_t transfer_id, std::uint32_t entry,
                         const std::filesystem::path& path, std::uint64_t bytes, bool complete) = 0;
    virtual void finished(std::uint64_t transfer_id, osdrop_result result,
                          const std::vector<std::filesystem::path>& items) = 0;
};

/// Hands core's transfer events to the sinks that asked for them. A transfer's first events can
/// come before receive() returned its ID; they are kept until the sink claims it.
class TransferRouter {
public:
    /// Brackets a receive(): events for IDs nobody claimed are kept meanwhile.
    void starting();
    /// The receive() returned `transfer_id` (nullopt: it failed); `sink` gets its events, those
    /// kept included.
    void started(std::optional<std::uint64_t> transfer_id, const std::weak_ptr<TransferSink>& sink);

    // Core's events, on its thread.
    void planned(std::uint64_t transfer_id, const std::vector<PlannedEntry>& entries);
    void written(std::uint64_t transfer_id, std::uint32_t entry, const std::filesystem::path& path,
                 std::uint64_t bytes, bool complete);
    void finished(std::uint64_t transfer_id, osdrop_result result,
                  const std::vector<std::filesystem::path>& items);

private:
    using Event = std::function<void(TransferSink&)>;
    /// The sink of `transfer_id`, or nullptr after keeping `event` for a receive() still running.
    std::shared_ptr<TransferSink> sink_or_keep(std::uint64_t transfer_id, Event event);

    std::mutex mutex_;
    int starting_ = 0;
    std::map<std::uint64_t, std::weak_ptr<TransferSink>> sinks_;
    std::vector<std::pair<std::uint64_t, Event>> kept_;
};

/// Starts and stops the transfers of drag items (the group engine; a fake in tests).
class DragTransfers {
public:
    virtual ~DragTransfers() = default;
    /// Receives item `index` of drag `drag_id` from `source` into `folder`; its events go to
    /// `sink`, and those that came before this returns reach it before it returns. The transfer's
    /// ID, or nullopt when it could not start.
    virtual std::optional<std::uint64_t> receive(const SourceId& source, std::uint32_t drag_id,
                                                 std::uint32_t index,
                                                 const std::filesystem::path& folder,
                                                 const std::weak_ptr<TransferSink>& sink) = 0;
    virtual void cancel(std::uint64_t transfer_id) = 0;
};

/// A file core is writing, read with sharing that lets core keep writing, rename and delete it.
class GrowingFile {
public:
    virtual ~GrowingFile() = default;
    /// Up to `buffer.size()` bytes at `offset`; nullopt when it cannot be read.
    [[nodiscard]] virtual std::optional<std::size_t> read(std::uint64_t offset,
                                                          std::span<std::uint8_t> buffer) = 0;
};
/// Opens a file for reading as above; null when it cannot be opened (yet).
using FileOpener = std::function<std::unique_ptr<GrowingFile>(const std::filesystem::path&)>;

/// A top-level item of an arriving drag, as its DragOffer named it.
struct ArrivingItem {
    std::string name; ///< UTF-8, as mapped for this computer
    bool folder = false;
    std::uint64_t size = 0;
};

class NetworkDrag final : public ContentSource,
                          public TransferSink,
                          public std::enable_shared_from_this<NetworkDrag> {
public:
    /// Must be owned by a shared_ptr before start().
    NetworkDrag(DragTransfers& transfers, const SourceId& source, std::uint32_t drag_id,
                std::vector<ArrivingItem> items, std::filesystem::path staging, FileOpener opener);

    /// Starts every item's transfer, each into its own folder under the staging folder; false
    /// when one could not start (the others are cancelled). Never from core's thread.
    [[nodiscard]] bool start();
    /// Every file and folder, parents first, once every item is planned; nullopt when a transfer
    /// failed, the drag was cancelled or `timeout` passed. Links are left out.
    [[nodiscard]] std::optional<std::vector<TreeEntry>> entries(std::chrono::milliseconds timeout);
    /// A reader of entry `entry` of entries(), which follows the file as it is written.
    [[nodiscard]] std::unique_ptr<ContentReader> open(std::size_t entry) override;
    /// The received top-level items once every transfer ended well; nullopt otherwise.
    [[nodiscard]] std::optional<std::vector<std::filesystem::path>>
    received(std::chrono::milliseconds timeout);
    /// Stops the transfers still running and ends every read. Never from core's thread.
    void cancel();

    [[nodiscard]] const std::vector<ArrivingItem>& items() const { return items_; }
    /// Each item's transfer, once start() started it.
    [[nodiscard]] std::vector<std::optional<std::uint64_t>> transfers();
    [[nodiscard]] std::uint64_t total_bytes() const;
    [[nodiscard]] const std::filesystem::path& staging() const { return staging_; }

    // TransferSink, on core's thread.
    void planned(std::uint64_t transfer_id, const std::vector<PlannedEntry>& entries) override;
    void written(std::uint64_t transfer_id, std::uint32_t entry, const std::filesystem::path& path,
                 std::uint64_t bytes, bool complete) override;
    void finished(std::uint64_t transfer_id, osdrop_result result,
                  const std::vector<std::filesystem::path>& items) override;

private:
    class Reader;
    struct File {
        std::filesystem::path path; ///< empty until written
        std::uint64_t bytes = 0;
        bool complete = false;
    };
    struct Item {
        std::optional<std::uint64_t> transfer;
        std::optional<std::vector<PlannedEntry>> plan;
        std::map<std::uint32_t, File> files; ///< by the transfer's entry index
        bool done = false;
        std::vector<std::filesystem::path> received;
    };
    struct Place {
        std::size_t item = 0;
        std::uint32_t entry = 0;
    };

    /// The item of a transfer; under mutex_.
    [[nodiscard]] Item* item_of(std::uint64_t transfer_id);
    [[nodiscard]] bool broken() const { return failed_ || cancelled_; } // under mutex_

    DragTransfers& transfers_;
    SourceId source_;
    std::uint32_t drag_id_;
    std::vector<ArrivingItem> items_;
    std::filesystem::path staging_;
    FileOpener opener_;

    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<Item> state_;                  ///< per item
    std::optional<std::vector<Place>> places_; ///< per entry of entries(), once known
    /// The item whose receive() runs: its first events may come before its ID is known.
    std::optional<std::size_t> starting_;
    bool failed_ = false;
    bool cancelled_ = false;
};

} // namespace osdrop::win::drag
