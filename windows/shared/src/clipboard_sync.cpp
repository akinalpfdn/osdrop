#include <osdrop/win/clipboard_sync.hpp>

#include <algorithm>
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

osdrop_result CoreClipboardMesh::copied(const ClipData& data) {
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
    std::uint64_t clip = 0;
    return osdrop_mesh_clipboard_copied(mesh_, contents.data(), contents.size(), files.data(),
                                        files.size(), &clip);
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

ClipboardSync::ClipboardSync(ClipboardHost& host, Notify notify, ClipboardLimits limits)
    : host_(host), notify_(std::move(notify)), limits_(limits) {}

void ClipboardSync::attach(ClipboardMesh& mesh) {
    host_.post([this, &mesh] { mesh_ = &mesh; });
}

void ClipboardSync::copied(ClipData data) {
    if (connected_.empty() || mesh_ == nullptr || (data.contents.empty() && data.files.empty())) {
        return; // nobody to share with: the copy is not even offered
    }
    std::erase_if(data.contents, [&](const ClipContent& content) {
        return content.bytes.size() > limits_.limit(content.format);
    });
    incoming_.reset(); // this copy is newer than the one being fetched
    if (!data.contents.empty() || !data.files.empty()) {
        static_cast<void>(mesh_->copied(data));
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
    host_.post(
        [this, device, clip_id, formats = std::move(formats)] { begin(device, clip_id, formats); });
}

void ClipboardSync::content_received(std::uint64_t id, osdrop_result result,
                                     std::vector<std::uint8_t> content) {
    host_.post([this, id, result, content = std::move(content)]() mutable {
        settle(id, result, std::move(content), {});
    });
}

void ClipboardSync::transfer_finished(std::uint64_t id, osdrop_result result,
                                      std::vector<std::string> items) {
    host_.post([this, id, result, items = std::move(items)]() mutable {
        settle(id, result, {}, std::move(items));
    });
}

void ClipboardSync::begin(const ClipDevice& device, std::uint64_t clip_id,
                          const std::vector<osdrop_announced_format>& formats) {
    if (mesh_ == nullptr) {
        return;
    }
    incoming_ = Incoming{.device = device, .clip_id = clip_id};
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
            notify_(ClipboardNotice::TooLarge, device, name_of(device));
        }
        incoming_.reset();
        return;
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
    if (copy.received.contents.empty() && copy.received.files.empty()) {
        host_.clear_staging(std::nullopt);
        notify_(copy.too_large ? ClipboardNotice::TooLarge : ClipboardNotice::Unavailable,
                copy.device, name_of(copy.device));
        return;
    }
    host_.write(copy.received);
    host_.clear_staging(copy.received.files.empty() ? std::nullopt : copy.folder);
    if (copy.too_large) {
        notify_(ClipboardNotice::TooLarge, copy.device, name_of(copy.device));
    }
}

std::string ClipboardSync::name_of(const ClipDevice& device) const {
    const auto it = connected_.find(device);
    return it == connected_.end() ? std::string() : it->second;
}

} // namespace osdrop::win
