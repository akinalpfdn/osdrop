#include <osdrop/win/clipboard_sync.hpp>

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>

namespace osdrop::win {

std::uint64_t ClipboardLimits::limit(osdrop_clipboard_format format) const {
    switch (format) {
    case OSDROP_CLIPBOARD_IMAGE:
        return image;
    case OSDROP_CLIPBOARD_FILES:
        return files;
    default:
        return text;
    }
}

CopySignature CopySignature::of(const ClipData& data) {
    CopySignature signature;
    for (const ClipContent& content : data.contents) {
        const std::string_view bytes(reinterpret_cast<const char*>(content.bytes.data()), // NOLINT
                                     content.bytes.size());
        signature.contents.push_back({.format = content.format,
                                      .size = content.bytes.size(),
                                      .hash = std::hash<std::string_view>{}(bytes)});
    }
    for (const std::string& file : data.files) {
        const std::filesystem::path path(std::u8string(file.begin(), file.end()));
        std::error_code error;
        const bool regular = std::filesystem::is_regular_file(path, error);
        const std::uintmax_t size = regular ? std::filesystem::file_size(path, error) : 0;
        const auto modified = std::filesystem::last_write_time(path, error);
        signature.files.push_back(
            {.path = file,
             .size = error ? 0 : size,
             .modified =
                 error ? 0 : static_cast<std::int64_t>(modified.time_since_epoch().count())});
    }
    return signature;
}

osdrop_result CoreClipboardMesh::copied(const ClipData& data, std::uint64_t& clip_id) {
    std::vector<osdrop_clipboard_content> contents;
    contents.reserve(data.contents.size());
    for (const ClipContent& content : data.contents) {
        contents.push_back(
            {.format = content.format, .data = content.bytes.data(), .size = content.bytes.size()});
    }
    std::vector<const char*> files;
    files.reserve(data.files.size());
    for (const std::string& file : data.files) {
        files.push_back(file.c_str());
    }
    return osdrop_mesh_clipboard_copied(mesh_, contents.data(), contents.size(), files.data(),
                                        files.size(), &clip_id);
}

void CoreClipboardMesh::withdraw(std::uint64_t clip_id) {
    static_cast<void>(osdrop_mesh_withdraw(mesh_, OSDROP_OBJECT_CLIPBOARD, clip_id)); // gone: fine
}

osdrop_result CoreClipboardMesh::fetch_content(const ClipDevice& device,
                                               const osdrop_object_ref& item,
                                               std::uint64_t max_bytes, std::uint64_t& id) {
    return osdrop_mesh_fetch_content(mesh_, device.data(), &item, max_bytes, &id);
}

osdrop_result CoreClipboardMesh::receive(const ClipDevice& device, const osdrop_object_ref& item,
                                         const std::string& folder, std::uint64_t max_bytes,
                                         std::uint64_t& id) {
    return osdrop_mesh_receive(mesh_, device.data(), &item, folder.c_str(), max_bytes, &id);
}

void CoreClipboardMesh::cancel(std::uint64_t id) {
    static_cast<void>(osdrop_mesh_cancel_transfer(mesh_, id)); // an unknown ID already ended
}

ClipboardSync::ClipboardSync(ClipboardHost& host, Notify notify, Progress progress,
                             ClipboardLimits limits)
    : host_(host), notify_(std::move(notify)), progress_(std::move(progress)), limits_(limits) {}

void ClipboardSync::attach(ClipboardMesh& mesh) {
    host_.post([this, &mesh] { mesh_ = &mesh; });
}

void ClipboardSync::copied(ClipData data) {
    if (!shared_ || connected_.empty() || mesh_ == nullptr ||
        (data.contents.empty() && data.files.empty())) {
        return; // nobody to share with: the copy is not even offered
    }
    std::erase_if(data.contents, [&](const ClipContent& content) {
        return content.bytes.size() > limits_.limit(content.format);
    });
    CopySignature signature = CopySignature::of(data);
    if (signature == last_offered_) {
        return; // the same copy again: the others have it already
    }
    drop_incoming(); // this copy is newer than the one being fetched
    if (data.contents.empty() && data.files.empty()) {
        last_offered_.reset();
        return;
    }
    std::uint64_t clip = 0;
    const bool offered = mesh_->copied(data, clip) == OSDROP_OK;
    last_offered_ = offered ? std::optional(std::move(signature)) : std::nullopt;
    offered_clip_ = offered ? clip : 0;
}

void ClipboardSync::set_sharing(bool shared, std::uint64_t files_limit) {
    host_.post([this, shared, files_limit] {
        limits_.files = files_limit;
        if (shared_ == shared) {
            return;
        }
        shared_ = shared;
        if (shared || mesh_ == nullptr) {
            return;
        }
        last_offered_.reset();
        if (offered_clip_ != 0) {
            mesh_->withdraw(std::exchange(offered_clip_, 0));
        }
        if (incoming_) {
            for (const auto& [id, format] : incoming_->pending) {
                mesh_->cancel(id); // its end arrives for a copy no longer waited for
            }
            drop_incoming();
        }
    });
}

void ClipboardSync::cancel_fetch() {
    host_.post([this] {
        if (!incoming_ || !incoming_->fetch || mesh_ == nullptr) {
            return;
        }
        for (const auto& [id, format] : incoming_->pending) {
            mesh_->cancel(id); // its end arrives for a copy no longer waited for
        }
        drop_incoming();
    });
}

void ClipboardSync::drop_incoming() {
    const bool fetching = incoming_ && incoming_->fetch;
    incoming_.reset();
    if (fetching && progress_) {
        progress_(std::nullopt);
    }
}

void ClipboardSync::peer_connected(const ClipDevice& device, std::string name) {
    host_.post([this, device, name = std::move(name)] { connected_[device] = name; });
}

void ClipboardSync::peer_disconnected(const ClipDevice& device) {
    host_.post([this, device] { connected_.erase(device); });
}

void ClipboardSync::clipboard_changed(const ClipDevice& device, std::uint64_t clip_id,
                                      std::vector<osdrop_announced_format> formats) {
    host_.post([this, device, clip_id, formats = std::move(formats)] {
        if (!shared_) {
            return; // sharing turned off in Settings
        }
        last_offered_.reset(); // a copy here, even the same one, is newer than this
        begin(device, clip_id, formats);
    });
}

void ClipboardSync::content_received(std::uint64_t id, osdrop_result result,
                                     std::vector<std::uint8_t> content) {
    host_.post([this, id, result, content = std::move(content)]() mutable {
        settle(id, result, std::move(content), {});
    });
}

void ClipboardSync::transfer_finished(std::uint64_t id, osdrop_result result,
                                      std::vector<std::string> items, std::uint64_t skipped_links) {
    host_.post([this, id, result, items = std::move(items), skipped_links]() mutable {
        if (incoming_ && incoming_->pending.contains(id)) {
            incoming_->skipped_links += skipped_links;
        }
        settle(id, result, {}, std::move(items));
    });
}

void ClipboardSync::transfer_progress(std::uint64_t id, std::uint64_t bytes_done) {
    host_.post([this, id, bytes_done] {
        if (!incoming_ || !incoming_->fetch) {
            return;
        }
        const auto pending = incoming_->pending.find(id);
        if (pending == incoming_->pending.end() || pending->second != OSDROP_CLIPBOARD_FILES) {
            return;
        }
        incoming_->fetch->bytes_done = bytes_done;
        if (progress_) {
            progress_(incoming_->fetch);
        }
    });
}

void ClipboardSync::begin(const ClipDevice& device, std::uint64_t clip_id,
                          const std::vector<osdrop_announced_format>& formats) {
    if (mesh_ == nullptr) {
        return;
    }
    drop_incoming(); // a newer copy
    incoming_ = Incoming{.device = device,
                         .clip_id = clip_id,
                         .requests = 0,
                         .settled = 0,
                         .pending = {},
                         .received = {},
                         .folder = std::nullopt,
                         .too_large = false,
                         .fetch = std::nullopt};
    std::vector<osdrop_clipboard_format> wanted;
    for (const osdrop_announced_format& format : formats) {
        if (format.byte_count > limits_.limit(format.format)) {
            incoming_->too_large = true;
        } else {
            wanted.push_back(format.format);
        }
    }
    if (std::ranges::find(wanted, OSDROP_CLIPBOARD_FILES) != wanted.end()) {
        incoming_->folder = host_.make_staging_folder();
        if (!incoming_->folder) {
            std::erase(wanted, OSDROP_CLIPBOARD_FILES);
        }
    }
    incoming_->requests = wanted.size();
    if (wanted.empty()) {
        if (incoming_->too_large) {
            notify_(ClipboardNotice::TooLarge, device, name_of(device), 0);
        }
        incoming_.reset();
        return;
    }
    for (const auto& format : formats) {
        if (format.format == OSDROP_CLIPBOARD_FILES &&
            std::ranges::find(wanted, OSDROP_CLIPBOARD_FILES) != wanted.end()) {
            incoming_->fetch = ClipboardFetch{.device = device,
                                              .name = name_of(device),
                                              .bytes_done = 0,
                                              .bytes_total = format.byte_count};
        }
    }
    if (incoming_->fetch && progress_) {
        progress_(incoming_->fetch);
    }
    for (const osdrop_clipboard_format format : wanted) {
        request(format);
    }
}

void ClipboardSync::request(osdrop_clipboard_format format) {
    if (!incoming_) {
        return;
    }
    Incoming& copy = *incoming_;
    const osdrop_object_ref item{.kind = OSDROP_OBJECT_CLIPBOARD,
                                 .object_id = copy.clip_id,
                                 .item_index = static_cast<std::uint32_t>(format)};
    std::uint64_t id = 0;
    const osdrop_result requested =
        format == OSDROP_CLIPBOARD_FILES
            ? mesh_->receive(copy.device, item, copy.folder.value_or(std::string()), limits_.files,
                             id)
            : mesh_->fetch_content(copy.device, item, limits_.limit(format), id);
    if (requested != OSDROP_OK) {
        ++copy.settled;
        finish();
        return;
    }
    copy.pending[id] = format;
}

void ClipboardSync::settle(std::uint64_t id, osdrop_result result,
                           std::vector<std::uint8_t> content, std::vector<std::string> items) {
    if (!incoming_) {
        return;
    }
    Incoming& copy = *incoming_;
    const auto pending = copy.pending.find(id);
    if (pending == copy.pending.end()) {
        return; // a copy that a newer one replaced
    }
    const osdrop_clipboard_format format = pending->second;
    copy.pending.erase(pending);
    if (result == OSDROP_OK && format == OSDROP_CLIPBOARD_FILES) {
        copy.received.files = std::move(items);
    } else if (result == OSDROP_OK) {
        copy.received.contents.push_back({.format = format, .bytes = std::move(content)});
    } else if (result == OSDROP_ERROR_TOO_LARGE) {
        copy.too_large = true;
    }
    ++copy.settled;
    finish();
}

void ClipboardSync::finish() {
    if (!incoming_ || incoming_->settled < incoming_->requests) {
        return;
    }
    const Incoming copy = std::move(*incoming_);
    incoming_.reset();
    if (copy.fetch && progress_) {
        progress_(std::nullopt);
    }
    if (copy.received.contents.empty() && copy.received.files.empty()) {
        host_.clear_staging(std::nullopt);
        notify_(copy.too_large ? ClipboardNotice::TooLarge : ClipboardNotice::Unavailable,
                copy.device, name_of(copy.device), 0);
        return;
    }
    host_.write(copy.received);
    host_.clear_staging(copy.received.files.empty() ? std::nullopt : copy.folder);
    if (copy.too_large) {
        notify_(ClipboardNotice::TooLarge, copy.device, name_of(copy.device), 0);
    }
    if (!copy.received.files.empty()) {
        notify_(ClipboardNotice::Ready, copy.device, name_of(copy.device), copy.skipped_links);
    }
}

std::string ClipboardSync::name_of(const ClipDevice& device) const {
    const auto it = connected_.find(device);
    return it == connected_.end() ? std::string() : it->second;
}

} // namespace osdrop::win
