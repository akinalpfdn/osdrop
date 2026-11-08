#pragma once

// The thread transfers do their file work on (DEVPLAN: file I/O never runs on the I/O thread,
// which carries input). Work runs in the order posted.

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace osdrop::transfer {

class FileThread {
public:
    FileThread();
    /// stop()
    ~FileThread();
    FileThread(const FileThread&) = delete;
    FileThread& operator=(const FileThread&) = delete;
    FileThread(FileThread&&) = delete;
    FileThread& operator=(FileThread&&) = delete;

    /// From any thread. Work posted after stop() is dropped.
    void post(std::function<void()> work);
    /// Runs what is queued, then joins the thread.
    void stop();

private:
    void run();

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> queue_;
    bool stopping_ = false;
    std::thread thread_; // declared last: starts once the rest exists
};

} // namespace osdrop::transfer
