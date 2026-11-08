#pragma once

// The client end of the input pipe (DEVPLAN Phase 3): the engine and the developer tool send
// requests to OsdropInput in this session and receive its reports on a thread of the client's own.
// The pipe is opened for identification only, which OsdropInput needs to check who connects.

#include <osdrop/sys/handles.hpp>
#include <osdrop/win/input_protocol.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace osdrop::sys {

class InputClient {
public:
    /// On the client's reader thread.
    using OnReport = std::function<void(const win::input::Report&)>;
    /// Once, on the reader thread, when the pipe broke; never after the destructor began.
    using OnClosed = std::function<void()>;

    /// Connects to OsdropInput in this session; nullptr when it is not running or refuses.
    [[nodiscard]] static std::unique_ptr<InputClient> connect(OnReport on_report,
                                                              OnClosed on_closed);
    ~InputClient();
    InputClient(const InputClient&) = delete;
    InputClient& operator=(const InputClient&) = delete;
    InputClient(InputClient&&) = delete;
    InputClient& operator=(InputClient&&) = delete;

    /// From any thread; false when the pipe broke.
    bool send(const win::input::Request& request);

private:
    InputClient(UniqueHandle pipe, OnReport on_report, OnClosed on_closed);
    void read_reports();

    UniqueHandle pipe_;
    UniqueHandle stop_;
    UniqueHandle written_;
    OnReport on_report_;
    OnClosed on_closed_;
    std::mutex send_mutex_;
    std::atomic<bool> closing_{false};
    std::thread reader_;
};

} // namespace osdrop::sys
