#pragma once

// OsdropInput's device: SendInput for injection, the hooks and the cursors for capture (DEVPLAN
// Phase 3). Positions are physical pixels of the virtual desktop; the process is per-monitor DPI
// aware, so they are what the hooks report too.

#include "cursors.hpp"
#include "hooks.hpp"

#include <osdrop/win/input_session.hpp>

namespace osdrop::input {

class Injector final : public win::input::InputDevice {
public:
    Injector(Hooks& hooks, CursorHider& cursors);
    ~Injector() override = default;
    Injector(const Injector&) = delete;
    Injector& operator=(const Injector&) = delete;
    Injector(Injector&&) = delete;
    Injector& operator=(Injector&&) = delete;

    void move(std::int32_t x, std::int32_t y) override;
    void press(win::input::Button button, bool down) override;
    void scroll(std::int32_t vertical, std::int32_t horizontal) override;
    void key(win::ScanCode scan, bool down) override;
    void start_capture(std::int32_t park_x, std::int32_t park_y) override;
    void stop_capture(std::int32_t x, std::int32_t y) override;
    void set_controlled(bool controlled) override;
    void set_watching(bool watching) override;
    void mask_menu_keys() override;
    void set_chords(const win::input::Chord& return_home,
                    const win::input::Chord& lock_input) override;
    [[nodiscard]] win::input::Stats stats() const override;

private:
    Hooks& hooks_;
    CursorHider& cursors_;
};

} // namespace osdrop::input
