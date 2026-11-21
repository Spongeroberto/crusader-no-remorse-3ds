#pragma once
// Port only: files written on a thread of their own, so a slow medium (the
// 3DS SD card takes about 150 ms for a saved game) does not stall a frame.
// Each file is written as write_file_replacing does, whole or not at all.

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace crusader {

class BackgroundWriter {
public:
    BackgroundWriter();
    // Writes what is still queued, then stops.
    ~BackgroundWriter();
    BackgroundWriter(const BackgroundWriter&) = delete;
    BackgroundWriter& operator=(const BackgroundWriter&) = delete;

    // Queues `bytes` for `path`. A file of the same path still queued is
    // replaced: only the last one asked for is written.
    void write(std::filesystem::path path, std::vector<std::uint8_t> bytes);
    // Returns once every queued file is written (or failed: the failure is
    // reported on stderr), so that reading them sees the last.
    void wait();

private:
    void run();

    std::mutex mutex_;
    std::condition_variable work_, idle_;
    std::deque<std::pair<std::filesystem::path, std::vector<std::uint8_t>>> queue_;
    bool busy_ = false;
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace crusader
