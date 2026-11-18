// Checks that streamed shapes (Game's stream_shapes) play exactly like
// SHAPES.FLX read whole: two games get the same input, frame by frame, and
// must draw the same pictures, through two map changes that free the old
// map's shapes and read the new map's. Needs the original data (data/, not in
// the repository) but no saved games: runs locally only.
// Usage: shape_stream_tests <data_dir>   (skips with exit code 0 when data is absent)

#include "engine/world/world.hpp"
#include "game/game.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>

using namespace crusader;

namespace {

int failures = 0;
constexpr std::uint32_t kFrameUs = 1'000'000 / 60;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

struct Pair {
    Game whole, streamed;
    Surface8 a{Game::kScreenWidth, Game::kScreenHeight}, b{Game::kScreenWidth, Game::kScreenHeight};
    int frame = 0, compared = 0, differing = 0;

    explicit Pair(const std::filesystem::path& data)
        : whole(data, std::filesystem::temp_directory_path() / "crusader-shape-stream-whole"),
          streamed(data, std::filesystem::temp_directory_path() / "crusader-shape-stream-streamed", {}, true) {}

    template <class Fn>
    void both(Fn&& fn) {
        fn(whole);
        fn(streamed);
    }

    // One frame of both games; every tenth is drawn and compared.
    void step(const char* phase) {
        both([](Game& g) { g.update(kFrameUs); });
        if (++frame % 10 != 0) return;
        RenderStats sa, sb;
        whole.render(a, &sa);
        streamed.render(b, &sb);
        ++compared;
        if (a.pixels != b.pixels || sa.drawn != sb.drawn) {
            if (differing++ < 5) std::fprintf(stderr, "  %s: frame %d differs\n", phase, frame);
        }
    }
};

// Where the player lands on another map: the first item of that map that
// lies on the ground.
std::optional<FixedRecord> landing(const std::filesystem::path& data, int map) {
    const auto fixed = FlexArchive::open(data / "STATIC/FIXED.DAT");
    for (const FixedRecord& r : read_fixed_map(fixed, map)) {
        if (!(r.flags & kItemContained)) return r;
    }
    return std::nullopt;
}

void run(const std::filesystem::path& data) {
    Pair p(data);
    p.both([](Game& g) { g.new_game(0); });

    // To play, as the bench does: Escape, held for one frame, skips the movies.
    // The first map loads only once the game's scripts run.
    for (int i = 0; i < 3600 && !(p.whole.mouse_in_play() && p.whole.world().current_map() > 0); ++i) {
        const bool press = i % 30 == 15, release = i % 30 == 16;
        if (press) p.both([](Game& g) { g.key(0x01, false, true, 0, 0x1B); });
        if (release) p.both([](Game& g) { g.key(0x01, false, false, 0); });
        p.step("intro");
    }
    check(p.whole.mouse_in_play() && p.streamed.mouse_in_play(), "both games reach play");
    const int first_map = p.whole.world().current_map();
    check(first_map > 0 && p.streamed.world().current_map() == first_map, "both games are on the first map");

    p.both([](Game& g) { g.key(0x48, true, true, 0); });  // walk
    for (int i = 0; i < 300; ++i) p.step("first map");
    const auto first = p.streamed.shape_stats();

    const int other_map = first_map == 2 ? 3 : 2;
    const auto at = landing(data, other_map);
    check(at.has_value(), "the other map has an item to land on");
    if (!at) return;
    p.both([&](Game& g) { g.world().teleport(1, at->x, at->y, at->z, other_map); });
    check(p.streamed.world().current_map() == other_map, "the player is on the other map");
    for (int i = 0; i < 300; ++i) p.step("other map");
    const auto second = p.streamed.shape_stats();

    const auto back = landing(data, first_map);
    p.both([&](Game& g) { g.world().teleport(1, back->x, back->y, back->z, first_map); });
    check(p.streamed.world().current_map() == first_map, "the player is back on the first map");
    for (int i = 0; i < 300; ++i) p.step("back on the first map");
    const auto third = p.streamed.shape_stats();

    std::printf("frames %d, compared %d, differing %d; maps %d -> %d -> %d\n", p.frame, p.compared, p.differing,
                first_map, other_map, p.streamed.world().current_map());
    for (const auto& [name, s] : {std::pair{"first map", first}, {"other map", second}, {"back", third}}) {
        std::printf("  %-10s %4zu shapes held, %6zu KiB, %zu read one by one, %zu file reads in all\n", name, s.shapes,
                    s.bytes / 1024, s.misses, s.requests);
    }
    check(p.differing == 0, "streamed shapes draw the same frames");
    check(p.whole.machine().faults().empty() && p.streamed.machine().faults().empty(), "no usecode faults");
    // Changing maps freed shapes: the held bytes stay those of about one map.
    check(second.bytes < 16u << 20 && third.bytes < 16u << 20, "about one map's shapes are held");
    check(third.misses - first.misses < 20, "few shapes are read one by one");
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path data = argc > 1 ? argv[1] : "data";
    if (!std::filesystem::exists(data / "STATIC/SHAPES.FLX")) {
        std::printf("shape_stream_tests: no game data at %s, skipped\n", data.string().c_str());
        return 0;
    }
    try {
        run(data);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        ++failures;
    }
    std::printf(failures ? "%d failure(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
