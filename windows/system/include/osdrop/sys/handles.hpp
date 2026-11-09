#pragma once

#include <windows.h>

#include <memory>
#include <type_traits>

namespace osdrop::sys {

struct HandleCloser {
    void operator()(HANDLE handle) const noexcept {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

struct LocalFreer {
    void operator()(void* memory) const noexcept { LocalFree(memory); }
};

/// A manual-reset (or auto-reset) event without a name; empty when it could not be created.
[[nodiscard]] inline UniqueHandle make_event(bool manual_reset) {
    return UniqueHandle(CreateEventW(nullptr, manual_reset ? TRUE : FALSE, FALSE, nullptr));
}

} // namespace osdrop::sys
