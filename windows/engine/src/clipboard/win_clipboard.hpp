#pragma once

// The Win32 clipboard for the shared clipboard (DEVPLAN, Phase 15). One thread owns a
// message-only window: it hears clipboard changes (AddClipboardFormatListener), reads a copy in
// the shared formats unless an application marked it as concealed, writes other computers'
// copies, and runs the sync's work in order. Images become PNG and DIB through WIC on this thread
// (COM single-threaded apartment). Files copied on other computers wait in a staging folder under
// %TEMP%\Osdrop\clipboard.

#include <osdrop/win/clipboard_sync.hpp>

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace osdrop::engine {

class WinClipboard final : public win::ClipboardHost {
public:
    WinClipboard();
    /// stop()
    ~WinClipboard() override;
    WinClipboard(const WinClipboard&) = delete;
    WinClipboard& operator=(const WinClipboard&) = delete;
    WinClipboard(WinClipboard&&) = delete;
    WinClipboard& operator=(WinClipboard&&) = delete;

    /// Where copies on this PC go, called on the clipboard thread.
    void listen(std::function<void(win::ClipData)> copied);
    /// Ends the thread; work posted afterwards is dropped. Called before the sync goes.
    void stop();

    // --- win::ClipboardHost ---
    void post(std::function<void()> work) override;
    void write(const win::ClipData& data) override;
    std::optional<std::string> make_staging_folder() override;
    void clear_staging(const std::optional<std::string>& kept) override;

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void run();
    void run_posted();
    void changed();

    std::mutex mutex_;
    std::condition_variable started_;
    std::deque<std::function<void()>> queue_; ///< guarded by mutex_
    bool stopping_ = false;                   ///< guarded by mutex_
    bool ready_ = false;                      ///< the window exists or never will; mutex_
    std::atomic<HWND> window_ = nullptr;
    std::function<void(win::ClipData)> copied_; ///< on the clipboard thread only
    std::thread thread_;                        // declared last: starts once the rest exists
};

} // namespace osdrop::engine
