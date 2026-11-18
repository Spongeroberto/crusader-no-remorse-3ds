// The benchmark loop shared by the PC tool (bench.cpp) and ports that run it
// on their own hardware (platform/3ds/bench): starts a new game, skips the
// movies, then plays the first map for a number of frames, timing the game
// update and the render separately.
#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace crusader {

struct BenchOptions {
    std::filesystem::path data = "data";
    // Where the game saves: the run autosaves, which must not replace the
    // player's own autosave, so the default is a folder in the system's
    // temporary directory. Ports without one point it somewhere writable.
    std::filesystem::path saves;
    int frames = 1800;
    int width = 640, height = 480;
    bool walk = false;  // up arrow held for the whole run
    bool fire = false;  // space tapped every 40 frames
    std::filesystem::path png;  // last frame, when set
    // Platform hooks. now_us: a monotonic clock in microseconds (default:
    // std::chrono::steady_clock). peak_memory_kb: the most memory used so far
    // (default: VmHWM from /proc, 0 where there is none). progress: called
    // every 60 timed frames.
    std::function<double()> now_us;
    std::function<long()> peak_memory_kb;
    std::function<void(int done, int total)> progress;
};

// Runs the benchmark and returns its results as one JSON object. Throws
// what the game throws (missing data, for instance).
std::string run_bench(const BenchOptions& options);

}  // namespace crusader
