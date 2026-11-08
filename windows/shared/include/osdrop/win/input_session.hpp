#pragma once

// OsdropInput's side of one pipe client (DEVPLAN Phase 3): requests are checked and carried out,
// injection only while the client controls this PC, and whatever it pressed is released when
// control ends or the client goes, so no key or button stays down. Portable, so the rules are
// tested on every host; the Windows process supplies the device and the log.

#include <osdrop/win/input_protocol.hpp>

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
    virtual void stop_capture(std::int32_t x, std::int32_t y) = 0;
    /// Whether another computer controls this PC: its own input is then reported as LocalInput.
    virtual void set_controlled(bool controlled) = 0;
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
    InputSession(InputDevice& device, InputLog& log);
    /// Does not reset: the owner calls reset() when the client goes or the process ends.
    ~InputSession() = default;
    InputSession(const InputSession&) = delete;
    InputSession& operator=(const InputSession&) = delete;
    InputSession(InputSession&&) = delete;
    InputSession& operator=(InputSession&&) = delete;

    /// Handles one pipe message; returns a report to send back (Stats, Rejected) when there is one.
    [[nodiscard]] std::optional<Report> handle(std::span<const std::uint8_t> message);
    /// The client is gone: capture ends where it parked, and what it pressed is released.
    void reset();

    [[nodiscard]] bool controlling() const { return controlling_; }
    [[nodiscard]] bool capturing() const { return capture_.has_value(); }

private:
    struct Park {
        std::int32_t x = 0;
        std::int32_t y = 0;
    };

    std::optional<Report> carry_out(const Request& request);
    std::optional<Report> refuse(std::uint16_t type, Refusal reason, std::string_view why);
    void release_all();

    InputDevice& device_;
    InputLog& log_;
    bool controlling_ = false;
    std::optional<Park> capture_;
    std::set<ScanCode> keys_down_;
    std::set<Button> buttons_down_;
};

} // namespace osdrop::win::input
