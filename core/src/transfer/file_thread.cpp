#include "transfer/file_thread.hpp"

#include <exception>
#include <utility>

namespace osdrop::transfer {

FileThread::FileThread() : thread_([this] { run(); }) {}

FileThread::~FileThread() {
    try {
        stop();
    } catch (...) {
        // Only locking and joining can throw; a thread that cannot be stopped is unrecoverable,
        // and this makes the implicit terminate explicit.
        std::terminate();
    }
}

void FileThread::post(std::function<void()> work) {
    {
        const std::scoped_lock lock(mutex_);
        if (stopping_) {
            return;
        }
        queue_.push_back(std::move(work));
    }
    wake_.notify_one();
}

void FileThread::stop() {
    {
        const std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_one();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) {
        thread_.join();
    }
}

void FileThread::run() {
    for (;;) {
        std::function<void()> work;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                return; // stopping, and everything queued ran
            }
            work = std::move(queue_.front());
            queue_.pop_front();
        }
        work();
    }
}

} // namespace osdrop::transfer
