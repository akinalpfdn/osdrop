#pragma once

// OsdropInput's side of one pipe client (DEVPLAN Phases 3 and 11): requests are checked and
// carried out, injection only while the client controls this PC or captures its input, and
// whatever it pressed is released when control or capture ends or the client goes, so no key or
// button stays down. A capture lasts only while the client confirms it, and the return chord ends
// it here whatever the client does. Portable, so the rules are tested on every host; the Windows
// process supplies the device, the log and the clock.

#include <osdrop/win/input_protocol.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string_view>

namespace osdrop::win::input {

/// What OsdropInput does to the system: SendInput and the hooks on Windows, a fake in tests.
class InputDevice {
public:
    virtual ~InputDevice() = default;
    virtual void move(std::int32_t x, std::int32_t y) = 0;
    virtual void press(Button button, bool down) = 0;
    virtual void scroll(std::int32_t vertical, std::int32_t horizontal) = 0;
    virtual void key(ScanCode scan, bool down) = 0;
    virtual void start_capture(std::int32_t park_x, std::int32_t park_y) = 0;
    /// Also when the capture already ended here (Released): the cursor goes to (x, y).
    virtual void stop_capture(std::int32_t x, std::int32_t y) = 0;
    /// Whether another computer controls this PC: its own input is then reported as LocalInput.
    virtual void set_controlled(bool controlled) = 0;
    /// Whether this PC's own input is reported as it acts here, and the chords are Osdrop's.
    virtual void set_watching(bool watching) = 0;
    /// A chord's key was kept from the apps while its modifiers reached them: a key nobody uses is
    /// pressed, so releasing Windows or Alt opens no menu.
    virtual void mask_menu_keys() = 0;
    /// The chords the hooks match from now on (a valid pair).
    virtual void set_chords(const Chord& return_home, const Chord& lock_input) = 0;
    [[nodiscard]] virtual Stats stats() const = 0;
};

class InputLog {
public:
    virtual ~InputLog() = default;
    virtual void info(std::string_view message) = 0;
    virtual void warning(std::string_view message) = 0;
};

class InputSession {
public:
    using Clock = std::chrono::steady_clock;

    InputSession(InputDevice& device, InputLog& log);
    /// Does not reset: the owner calls reset() when the client goes or the process ends.
    ~InputSession() = default;
    InputSession(const InputSession&) = delete;
    InputSession& operator=(const InputSession&) = delete;
    InputSession(InputSession&&) = delete;
    InputSession& operator=(InputSession&&) = delete;

    /// Handles one pipe message; returns a report to send back (Stats, Rejected) when there is one.
    [[nodiscard]] std::optional<Report> handle(std::span<const std::uint8_t> message,
                                               Clock::time_point now);
    /// A report from the hooks, on its way to the client: the return chord ends the capture here.
    void forwarding(const Report& report);
    /// When the capture ends unless a Heartbeat comes first; nullopt while nothing is captured.
    [[nodiscard]] std::optional<Clock::time_point> deadline() const;
    /// Past the deadline the capture ends here; the Released to send the client.
    [[nodiscard]] std::optional<Report> expire(Clock::time_point now);
    /// The client is gone: capture ends where the cursor left, what it pressed is released (a
    /// held drag's left button too), and this PC's input is no longer watched.
    void reset();

    [[nodiscard]] bool controlling() const { return controlling_; }
    /// This PC's input is captured now: StartCapture came, and neither StopCapture nor Released.
    [[nodiscard]] bool capturing() const { return capture_ && !capture_->released; }
    [[nodiscard]] bool watching() const { return watching_; }

private:
    struct Capture {
        std::int32_t home_x = 0;
        std::int32_t home_y = 0;
        Clock::time_point deadline;
        bool released = false; ///< ended here; the client's StopCapture is still to come
    };

    std::optional<Report> carry_out(const Request& request, Clock::time_point now);
    void inject(const Request& request);
    std::optional<Report> set_control(bool controlling);
    std::optional<Report> start_capture(const StartCapture& start, Clock::time_point now);
    std::optional<Report> stop_capture(const StopCapture& stop);
    std::optional<Report> heartbeat(Clock::time_point now);
    std::optional<Report> watch(bool on);
    std::optional<Report> set_chords(const SetChords& chords);
    std::optional<Report> hold_drag();
    std::optional<Report> release_drag();
    /// Ends a capture that is still on here; whether there was one.
    bool end_capture_here();
    std::optional<Report> refuse(std::uint16_t type, Refusal reason, std::string_view why);
    void release_all();

    InputDevice& device_;
    InputLog& log_;
    bool controlling_ = false;
    bool watching_ = false;
    SetChords chords_; ///< as the client last set them; the defaults until then
    std::optional<Capture> capture_;
    std::set<ScanCode> keys_down_;
    std::set<Button> buttons_down_;
    bool drag_held_ = false; ///< the left button stays down past the control (HoldDrag)
};

} // namespace osdrop::win::input
