#include "tools/bench_run.hpp"

#include "core/png_writer.hpp"
#include "core/flex.hpp"
#include "game/game.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <vector>

namespace crusader {

namespace {

constexpr std::uint32_t kFrameUs = 1'000'000 / 60;

double steady_now_us() {
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

long proc_peak_rss_kb() {
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

std::string format(const char* fmt, auto... args) {
    char buffer[512];
    std::snprintf(buffer, sizeof buffer, fmt, args...);
    return buffer;
}

std::string summary_json(const char* name, const Summary& s) {
    return format("  \"%s\": {\"mean_us\": %.1f, \"p50_us\": %.1f, \"p95_us\": %.1f, \"max_us\": %.1f}", name,
                  s.mean_us, s.p50_us, s.p95_us, s.max_us);
}

// FNV-1a, 64 bits.
void hash_bytes(std::uint64_t& h, const std::uint8_t* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
}

// Where the player lands on a map: its first item lying on the ground.
std::optional<FixedRecord> landing(const std::filesystem::path& data, int map) {
    const auto fixed = FlexArchive::open(data / "STATIC/FIXED.DAT");
    for (const FixedRecord& r : read_fixed_map(fixed, static_cast<std::size_t>(map))) {
        if (!(r.flags & kItemContained)) return r;
    }
    return std::nullopt;
}

void teleport_player(Game& game, const std::filesystem::path& data, int map) {
    if (const auto at = landing(data, map)) game.world().teleport(1, at->x, at->y, at->z, map);
}

void tap(Game& game, std::uint8_t scancode, std::uint8_t ascii = 0) {
    game.key(scancode, false, true, 0, ascii);
    game.key(scancode, false, false, 0);
}

}  // namespace

std::string run_bench(const BenchOptions& o) {
    const auto now = o.now_us ? o.now_us : steady_now_us;
    const auto peak_kb = o.peak_memory_kb ? o.peak_memory_kb : proc_peak_rss_kb;

    const double t0 = now();
    const auto saves =
        o.saves.empty() ? std::filesystem::temp_directory_path() / "crusader-bench-saves" : o.saves;
    Game game(o.data, saves, {}, o.stream_shapes);
    game.new_game(0);
    const double load_ms = (now() - t0) / 1000.0;
    const long peak_after_load = peak_kb();

    Surface8 screen = o.half ? Surface8::half(o.width, o.height) : Surface8(o.width, o.height);
    // The map loads and the briefing movie starts only once the kernel runs,
    // so play begins when the world is drawn with no movie or screen up.
    // Escape skips what comes before: the movie player polls whether the key
    // is held, so it stays down for a frame. Give up after a minute of game
    // time so a screen that ignores it cannot hang the run.
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
    if (o.walk) game.key(0x48, true, true, 0);

    game.set_profile_clock(now);
    game.reset_profile();
    std::vector<double> update_us, render_us;
    update_us.reserve(o.frames);
    render_us.reserve(o.frames);
    std::size_t drawn_total = 0;
    std::uint64_t frames_hash = 0xCBF29CE484222325ull;
    const int first_map = game.world().current_map();
    const int other_map = first_map == 2 ? 3 : 2;
    for (int i = 0; i < o.frames; ++i) {
        if (o.fire && i % 40 == 0) tap(game, 0x39, ' ');
        if (o.map_tour && i == o.frames / 3) teleport_player(game, o.data, other_map);
        if (o.map_tour && i == 2 * o.frames / 3) teleport_player(game, o.data, first_map);
        const double a = now();
        game.update(kFrameUs);
        const double b = now();
        RenderStats stats;
        game.render(screen, &stats);
        const double c = now();
        update_us.push_back(b - a);
        render_us.push_back(c - b);
        drawn_total += stats.drawn;
        if (o.hash) {
            hash_bytes(frames_hash, screen.pixels.data(), screen.pixels.size());
            hash_bytes(frames_hash, &game.palette().colors[0].r, sizeof game.palette().colors);
        }
        if (o.progress && (i + 1) % 60 == 0) o.progress(i + 1, o.frames);
    }

    if (!o.png.empty()) {
        // At half resolution, the stored pixels: the picture at half size.
        Surface8 stored(screen.stored_width(), screen.stored_height());
        stored.pixels = screen.pixels;
        write_png(o.png, stored, game.palette());
    }
    if (o.last_frame) o.last_frame(screen, game.palette());
    std::string json = "{\n";
    json += format("  \"half\": %s,\n", o.half ? "true" : "false");
    json += format("  \"size\": \"%dx%d\", \"frames\": %d, \"walk\": %s, \"fire\": %s,\n", screen.width,
                   screen.height, o.frames, o.walk ? "true" : "false", o.fire ? "true" : "false");
    json += format("  \"load_ms\": %.1f, \"skip_frames\": %d, \"in_play\": %s, \"menu_open_at_end\": %s,\n", load_ms,
                   skip_frames, in_play ? "true" : "false", game.menu() ? "true" : "false");
    json += format("  \"items_drawn_mean\": %.1f, \"world_items\": %zu,\n",
                   o.frames ? static_cast<double>(drawn_total) / o.frames : 0.0, game.world().item_count());
    json += summary_json("update", summarize(update_us)) + ",\n";
    json += summary_json("render", summarize(render_us)) + ",\n";
    json += format("  \"peak_memory_kb_after_load\": %ld, \"peak_memory_kb\": %ld,\n", peak_after_load, peak_kb());
    const auto& p = game.profile();
    const double n = o.frames ? o.frames : 1;
    json += format("  \"update_phases_us\": {\"kernel\": %.0f, \"scenery\": %.0f, \"audio\": %.0f},\n",
                   p.kernel_us / n, p.scenery_us / n, p.audio_us / n);
    json += format("  \"render_phases_us\": {\"clear\": %.0f, \"list\": %.0f, \"prepare\": %.0f, "
                   "\"draw\": %.0f, \"overlay\": %.0f},\n",
                   p.clear_us / n, p.list_us / n, p.prepare_us / n, p.draw_us / n, p.overlay_us / n);
    const auto shapes = game.shape_stats();
    json += format("  \"stream_shapes\": %s, \"shapes_held\": %zu, \"shape_kb\": %zu, \"shape_misses\": %zu, "
                   "\"shape_reads\": %zu,\n",
                   o.stream_shapes ? "true" : "false", shapes.shapes, shapes.bytes / 1024, shapes.misses,
                   shapes.requests);
    if (o.hash) {
        json += format("  \"frames_hash\": \"%016llx\", \"map_tour\": %s, \"map_at_end\": %d,\n",
                       static_cast<unsigned long long>(frames_hash), o.map_tour ? "true" : "false",
                       game.world().current_map());
    }
    json += format("  \"usecode_faults\": %u\n}\n", static_cast<unsigned>(game.machine().faults().size()));
    return json;
}

}  // namespace crusader
