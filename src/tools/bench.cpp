// Frame-time and memory benchmark: starts a new game, skips the movies, then
// plays the first map for a number of frames at a given screen size, timing
// the game update and the render separately. It is the yardstick for ports
// to slower hardware: the same loop (bench_run.cpp) runs on the console, so
// the numbers compare directly.
//
// Usage: bench [data_dir] [--frames N] [--size WxH] [--walk] [--fire] [--stream-shapes] [--png FILE]
// Prints one JSON object on stdout.

#include "tools/bench_run.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

int main(int argc, char** argv) {
    crusader::BenchOptions options;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) options.frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc)
            std::sscanf(argv[++i], "%dx%d", &options.width, &options.height);
        else if (std::strcmp(argv[i], "--walk") == 0) options.walk = true;
        else if (std::strcmp(argv[i], "--fire") == 0) options.fire = true;
        else if (std::strcmp(argv[i], "--stream-shapes") == 0) options.stream_shapes = true;
        else if (std::strcmp(argv[i], "--png") == 0 && i + 1 < argc) options.png = argv[++i];
        else options.data = argv[i];
    }
    try {
        std::fputs(crusader::run_bench(options).c_str(), stdout);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bench: %s\n", e.what());
        return 1;
    }
}
