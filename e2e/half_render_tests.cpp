// Checks that drawing straight into a half-resolution screen (Surface8::half)
// gives exactly the full-size picture with every other pixel of every other
// line kept: two games get the same input, frame by frame, one drawn at
// 800 x 480, one at half resolution, through the movies and menus at the
// start (drawn full size, then halved), walking, firing (casings, impact
// marks), the autosave icon and a visit to another map. Needs the original
// data (data/, not in the repository) but no saved games: runs locally only.
// Usage: half_render_tests <data_dir>   (skips with exit code 0 when data is absent)

#include "core/flex.hpp"
#include "engine/world/world.hpp"
#include "game/game.hpp"

#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <optional>

using namespace crusader;

namespace {

int failures = 0;
constexpr std::uint32_t kFrameUs = 1'000'000 / 60;
constexpr int kWidth = 800, kHeight = 480;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

struct Pair {
    Game full_game, half_game;
    Surface8 full{kWidth, kHeight};
    Surface8 half = Surface8::half(kWidth, kHeight);
    Surface8 expected = Surface8::half(kWidth, kHeight);
    int frame = 0, differing = 0, in_play = 0;

    explicit Pair(const std::filesystem::path& data)
        : full_game(data, std::filesystem::temp_directory_path() / "crusader-half-render-full"),
          half_game(data, std::filesystem::temp_directory_path() / "crusader-half-render-half") {}

    template <class Fn>
    void both(Fn&& fn) {
        fn(full_game);
        fn(half_game);
    }

    void step(const char* phase) {
        both([](Game& g) { g.update(kFrameUs); });
        full_game.render(full);
        half_game.render(half);
        keep_every_other_pixel(full, expected);
        ++frame;
        if (full_game.mouse_in_play()) ++in_play;
        const bool same_palette = std::memcmp(&full_game.palette(), &half_game.palette(), sizeof(Palette)) == 0;
        if (expected.pixels != half.pixels || !same_palette) {
            if (differing++ < 5) {
                std::size_t i = 0;
                while (i < half.pixels.size() && expected.pixels[i] == half.pixels[i]) ++i;
                const int w = half.stored_width();
                std::fprintf(stderr, "  %s: frame %d differs%s, first at (%d,%d)\n", phase, frame,
                             same_palette ? "" : " (palette)", 2 * static_cast<int>(i % w),
                             2 * static_cast<int>(i / w));
            }
        }
    }
};

std::optional<FixedRecord> landing(const std::filesystem::path& data, int map) {
    const auto fixed = FlexArchive::open(data / "STATIC/FIXED.DAT");
    for (const FixedRecord& r : read_fixed_map(fixed, static_cast<std::size_t>(map))) {
        if (!(r.flags & kItemContained)) return r;
    }
    return std::nullopt;
}

void run(const std::filesystem::path& data) {
    Pair p(data);
    // Off by default; on here so that their drawing is checked too.
    p.both([](Game& g) {
        g.config().impact_marks = g.config().blood_marks = g.config().casings = true;
        g.new_game(0);
    });

    // The movies and screens before play, skipped with Escape held a frame.
    for (int i = 0; i < 3600 && !(p.full_game.mouse_in_play() && p.full_game.world().current_map() > 0); ++i) {
        if (i % 30 == 15) p.both([](Game& g) { g.key(0x01, false, true, 0, 0x1B); });
        if (i % 30 == 16) p.both([](Game& g) { g.key(0x01, false, false, 0); });
        p.step("before play");
    }
    const int intro = p.frame;
    const int first_map = p.full_game.world().current_map();
    check(first_map > 0 && p.half_game.world().current_map() == first_map, "both games reach the first map");

    // Walking and firing: casings and impact marks; the autosave icon too.
    p.both([](Game& g) { g.key(0x48, true, true, 0); });
    for (int i = 0; i < 400; ++i) {
        if (i % 40 == 0) p.both([](Game& g) { g.key(0x39, false, true, 0, ' '); });
        if (i % 40 == 1) p.both([](Game& g) { g.key(0x39, false, false, 0); });
        p.step("first map");
    }
    // Standing, turning a step at a time and firing each way, so that shots
    // hit walls and leave impact marks.
    p.both([](Game& g) { g.key(0x48, true, false, 0); });
    for (int turn = 0; turn < 8; ++turn) {
        p.both([](Game& g) { g.key(0x4B, true, true, 0); });
        for (int i = 0; i < 8; ++i) p.step("turning");
        p.both([](Game& g) { g.key(0x4B, true, false, 0); });
        for (int i = 0; i < 60; ++i) {
            if (i % 20 == 0) p.both([](Game& g) { g.key(0x39, false, true, 0, ' '); });
            if (i % 20 == 1) p.both([](Game& g) { g.key(0x39, false, false, 0); });
            p.step("firing");
        }
    }

    const int other_map = first_map == 2 ? 3 : 2;
    for (const int map : {other_map, first_map}) {
        const auto at = landing(data, map);
        check(at.has_value(), "the map has an item to land on");
        if (!at) return;
        p.both([&](Game& g) { g.world().teleport(1, at->x, at->y, at->z, map); });
        for (int i = 0; i < 200; ++i) p.step(map == first_map ? "back on the first map" : "other map");
    }

    std::printf("frames %d (%d before play, %d in play), differing %d\n", p.frame, intro, p.in_play, p.differing);
    check(p.differing == 0, "the half-resolution screen is the full one with every other pixel kept");
    check(p.full_game.machine().faults().empty() && p.half_game.machine().faults().empty(), "no usecode faults");
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path data = argc > 1 ? argv[1] : "data";
    if (!std::filesystem::exists(data / "STATIC/SHAPES.FLX")) {
        std::printf("half_render_tests: no game data at %s, skipped\n", data.string().c_str());
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
