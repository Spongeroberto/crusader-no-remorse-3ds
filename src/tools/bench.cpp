// Frame-time and memory benchmark: starts a new game, skips the movies, then
// plays the first map for a number of frames at a given screen size, timing
// the game update and the render separately. It is the yardstick for the 3DS
// port: the same loop runs on the console, so the numbers compare directly.
//
// Usage: bench [data_dir] [--frames N] [--size WxH] [--walk] [--fire] [--png FILE]
// Prints one JSON object on stdout.

#include "core/png_writer.hpp"
#include "game/game.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace crusader;

namespace {

constexpr std::uint32_t kFrameUs = 1'000'000 / 60;

// Peak resident memory in KiB (VmHWM); 0 where /proc is not available.
long peak_rss_kb() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmHWM:", 0) == 0) return std::strtol(line.c_str() + 6, nullptr, 10);
    }
    return 0;
}

struct Summary {
    double mean_us = 0, p50_us = 0, p95_us = 0, max_us = 0;
};

Summary summarize(std::vector<double> us) {
    Summary s;
    if (us.empty()) return s;
    double total = 0;
    for (double v : us) total += v;
    s.mean_us = total / static_cast<double>(us.size());
    std::sort(us.begin(), us.end());
    s.p50_us = us[us.size() / 2];
    s.p95_us = us[std::min(us.size() - 1, us.size() * 95 / 100)];
    s.max_us = us.back();
    return s;
}

void print_summary(const char* name, const Summary& s) {
    std::printf("  \"%s\": {\"mean_us\": %.1f, \"p50_us\": %.1f, \"p95_us\": %.1f, \"max_us\": %.1f}", name, s.mean_us,
                s.p50_us, s.p95_us, s.max_us);
}

void tap(Game& game, std::uint8_t scancode, std::uint8_t ascii = 0) {
    game.key(scancode, false, true, 0, ascii);
    game.key(scancode, false, false, 0);
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path data = "data";
    int frames = 1800, width = 640, height = 480;
    bool walk = false, fire = false;
    std::filesystem::path png;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &width, &height);
        else if (std::strcmp(argv[i], "--walk") == 0) walk = true;
        else if (std::strcmp(argv[i], "--fire") == 0) fire = true;
        else if (std::strcmp(argv[i], "--png") == 0 && i + 1 < argc) png = argv[++i];
        else data = argv[i];
    }

    try {
        using clock = std::chrono::steady_clock;
        const auto t0 = clock::now();
        Game game(data);
        game.new_game(0);
        const double load_ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        const long rss_after_load = peak_rss_kb();

        Surface8 screen(width, height);
        // The map loads and the briefing movie starts only once the kernel
        // runs, so play begins when the world is drawn with no movie or screen
        // up. Escape skips what comes before: the movie player polls whether
        // the key is held, so it stays down for a frame. Give up after a minute
        // of game time so a screen that ignores it cannot hang the run.
        int skip_frames = 0;
        bool in_play = false;
        bool escape_held = false;
        for (; skip_frames < 3600; ++skip_frames) {
            if (escape_held) {
                game.key(0x01, false, false, 0);
                escape_held = false;
            } else if (!game.mouse_in_play() && skip_frames % 30 == 15) {
                game.key(0x01, false, true, 0, 0x1B);
                escape_held = true;
            }
            game.update(kFrameUs);
            RenderStats probe;
            game.render(screen, &probe);
            if (game.mouse_in_play() && probe.drawn > 0) {
                in_play = true;
                break;
            }
        }
        if (walk) game.key(0x48, true, true, 0);  // up arrow held

        std::vector<double> update_us, render_us;
        update_us.reserve(frames);
        render_us.reserve(frames);
        RenderStats stats;
        std::size_t drawn_total = 0;
        for (int i = 0; i < frames; ++i) {
            if (fire && i % 40 == 0) tap(game, 0x39, ' ');
            const auto a = clock::now();
            game.update(kFrameUs);
            const auto b = clock::now();
            stats = RenderStats{};
            game.render(screen, &stats);
            const auto c = clock::now();
            update_us.push_back(std::chrono::duration<double, std::micro>(b - a).count());
            render_us.push_back(std::chrono::duration<double, std::micro>(c - b).count());
            drawn_total += stats.drawn;
        }

        if (!png.empty()) write_png(png, screen, game.palette());
        std::printf("{\n");
        std::printf("  \"size\": \"%dx%d\", \"frames\": %d, \"walk\": %s, \"fire\": %s,\n", width, height, frames,
                    walk ? "true" : "false", fire ? "true" : "false");
        std::printf("  \"load_ms\": %.1f, \"skip_frames\": %d, \"in_play\": %s, \"menu_open_at_end\": %s,\n", load_ms,
                    skip_frames, in_play ? "true" : "false", game.menu() ? "true" : "false");
        std::printf("  \"items_drawn_mean\": %.1f,\n", frames ? static_cast<double>(drawn_total) / frames : 0.0);
        print_summary("update", summarize(update_us));
        std::printf(",\n");
        print_summary("render", summarize(render_us));
        std::printf(",\n  \"peak_rss_kb_after_load\": %ld, \"peak_rss_kb\": %ld,\n", rss_after_load, peak_rss_kb());
        std::printf("  \"usecode_faults\": %zu\n}\n", game.machine().faults().size());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bench: %s\n", e.what());
        return 1;
    }
}
