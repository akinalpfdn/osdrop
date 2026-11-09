#pragma once

// The cursor hides while this PC's input is captured: every system cursor is replaced by a
// transparent one, and the saved ones come back on release (DEVPLAN Phase 3). A marker file
// records that they are hidden, so a new OsdropInput puts cursors back after a crash.

#include <windows.h>

#include <filesystem>
#include <utility>
#include <vector>

namespace osdrop::input {

class CursorHider {
public:
    explicit CursorHider(std::filesystem::path marker);
    ~CursorHider();
    CursorHider(const CursorHider&) = delete;
    CursorHider& operator=(const CursorHider&) = delete;
    CursorHider(CursorHider&&) = delete;
    CursorHider& operator=(CursorHider&&) = delete;

    void hide();
    void restore();

    /// After a crash: cursors left hidden by an earlier OsdropInput are loaded again. As SYSTEM
    /// this loads the default scheme; the user's own scheme comes back from the client, which
    /// reloads the cursors as the user when the pipe breaks.
    static void recover(const std::filesystem::path& marker);

private:
    std::filesystem::path marker_;
    HCURSOR transparent_ = nullptr;
    std::vector<std::pair<DWORD, HCURSOR>> saved_;
    bool hidden_ = false;
};

} // namespace osdrop::input
