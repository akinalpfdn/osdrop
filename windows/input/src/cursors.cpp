#include "cursors.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <system_error>

namespace osdrop::input {
namespace {

// The system cursors (OCR_* in winuser.h, which needs OEMRESOURCE): arrow, I-beam, wait, cross,
// up arrow, the four sizing ones and move, unavailable, hand, working in background, help, pin and
// person.
constexpr std::array<DWORD, 16> kCursorIds{32512, 32513, 32514, 32515, 32516, 32642, 32643, 32644,
                                           32645, 32646, 32648, 32649, 32650, 32651, 32671, 32672};
constexpr int kCursorSize = 32;

HCURSOR make_transparent_cursor() {
    // AND mask all ones keeps the screen, XOR mask all zeros changes nothing: invisible.
    constexpr std::size_t kMaskBytes = kCursorSize * kCursorSize / 8;
    std::array<std::uint8_t, kMaskBytes> and_mask{};
    and_mask.fill(0xFF);
    const std::array<std::uint8_t, kMaskBytes> xor_mask{};
    return CreateCursor(GetModuleHandleW(nullptr), 0, 0, kCursorSize, kCursorSize, and_mask.data(),
                        xor_mask.data());
}

} // namespace

CursorHider::CursorHider(std::filesystem::path marker)
    : marker_(std::move(marker)), transparent_(make_transparent_cursor()) {}

CursorHider::~CursorHider() {
    restore();
    if (transparent_ != nullptr) {
        DestroyCursor(transparent_);
    }
}

void CursorHider::hide() {
    if (hidden_ || transparent_ == nullptr) {
        return;
    }
    {
        const std::ofstream marker(marker_);
    }
    for (const DWORD id : kCursorIds) {
        // The cursor in use now, the user's scheme included; the IDC_* and OCR_* numbers match.
        const HCURSOR shared = LoadCursorW(nullptr, MAKEINTRESOURCEW(id));
        if (shared == nullptr) {
            continue; // an older Windows without this cursor
        }
        const HCURSOR copy = CopyCursor(shared);
        const HCURSOR blank = CopyCursor(transparent_);
        if (copy != nullptr && blank != nullptr && SetSystemCursor(blank, id)) {
            saved_.emplace_back(id, copy); // SetSystemCursor took `blank`
            continue;
        }
        for (const HCURSOR unused : {copy, blank}) {
            if (unused != nullptr) {
                DestroyCursor(unused);
            }
        }
    }
    hidden_ = true;
}

void CursorHider::restore() {
    if (!hidden_) {
        return;
    }
    for (const auto& [id, cursor] : saved_) {
        SetSystemCursor(cursor, id); // takes ownership of the saved copy
    }
    saved_.clear();
    hidden_ = false;
    std::error_code error;
    std::filesystem::remove(marker_, error);
}

void CursorHider::recover(const std::filesystem::path& marker) {
    std::error_code error;
    if (std::filesystem::exists(marker, error)) {
        SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
        std::filesystem::remove(marker, error);
    }
}

} // namespace osdrop::input
