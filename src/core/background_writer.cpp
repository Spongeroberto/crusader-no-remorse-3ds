#include "core/background_writer.hpp"

#include "core/flex.hpp"

#include <cstdio>
#include <exception>

namespace crusader {

// @orig none: port only
BackgroundWriter::BackgroundWriter() : thread_([this] { run(); }) {}

BackgroundWriter::~BackgroundWriter() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    work_.notify_one();
    thread_.join();
}

void BackgroundWriter::write(std::filesystem::path path, std::vector<std::uint8_t> bytes) {
    {
        std::lock_guard lock(mutex_);
        bool replaced = false;
        for (auto& job : queue_) {
            if (job.first == path) {
                job.second = std::move(bytes);
                replaced = true;
                break;
            }
        }
        if (!replaced) queue_.emplace_back(std::move(path), std::move(bytes));
    }
    work_.notify_one();
}

void BackgroundWriter::wait() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return queue_.empty() && !busy_; });
}

void BackgroundWriter::run() {
    std::unique_lock lock(mutex_);
    for (;;) {
        work_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (queue_.empty()) return;  // stopping, everything written
        auto job = std::move(queue_.front());
        queue_.pop_front();
        busy_ = true;
        lock.unlock();
        try {
            write_file_replacing(job.first, job.second);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "write: %s\n", e.what());
        }
        lock.lock();
        busy_ = false;
        if (queue_.empty()) idle_.notify_all();
    }
}

}  // namespace crusader
