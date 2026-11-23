// Checks that the display list built from the map cells near the view (with a
// reach bound, shape_reach.hpp) is the list built from every item: a game is
// played through the start, walking, firing in eight directions and a visit
// to another map, and every frame both lists are built, at 800 x 480 and at
// 640 x 480, and compared item by item. Also checks the guards: a bound that
// a loaded shape exceeds is withdrawn, and a reach file made for another
// archive is refused. The scenery animation, which finds the items on screen
// the same way, is checked against walking every item. Needs the original data (data/, not in the repository)
// but no saved games: runs locally only.
// Usage: display_list_tests <data_dir>   (skips with exit code 0 when data is absent)

#include "core/flex.hpp"
#include "core/flex_file.hpp"
#include "core/shape_reach.hpp"
#include "engine/world/render.hpp"
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

struct Totals {
    int frames = 0, near_frames = 0, differing = 0;
    std::size_t visited_near = 0, visited_all = 0;
};

bool same_list(const std::vector<DrawItem>& a, const std::vector<DrawItem>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].item != b[i].item || a[i].frame != b[i].frame || a[i].sx != b[i].sx || a[i].sy != b[i].sy ||
            a[i].mirrored != b[i].mirrored) {
            return false;
        }
    }
    return true;
}

// Both lists for the view the game just drew, centred the same way for a
// smaller screen.
void compare(Game& game, ShapeCache& shapes, int w, int h, const char* phase, Totals& t) {
    const GameState& s = game.world().state;
    const View view{s.view_left + (s.screen_right + 1 - w) / 2, s.view_top + (s.screen_bottom + 1 - h) / 2};
    RenderStats near_stats, all_stats;
    const auto near = build_display_list(game.world(), shapes, view, w, h, &near_stats);
    const auto all = build_display_list(game.world(), shapes, view, w, h, &all_stats, nullptr, true);
    ++t.frames;
    if (near_stats.visited < all_stats.visited) ++t.near_frames;
    t.visited_near += near_stats.visited;
    t.visited_all += all_stats.visited;
    if (!same_list(near, all) && t.differing++ < 5) {
        std::fprintf(stderr, "  %s: %dx%d frame %d: %zu items near, %zu in all\n", phase, w, h, t.frames, near.size(),
                     all.size());
    }
}

std::optional<FixedRecord> landing(const std::filesystem::path& data, int map) {
    const auto fixed = FlexArchive::open(data / "STATIC/FIXED.DAT");
    for (const FixedRecord& r : read_fixed_map(fixed, static_cast<std::size_t>(map))) {
        if (!(r.flags & kItemContained)) return r;
    }
    return std::nullopt;
}

// Views all over the map, at a step that is no divisor of a cell, so that
// every kind of item meets every edge of the screen: the cells' bound
// matters most where a large frame reaches in from a cell off screen.
void sweep(const World& world, ShapeCache& shapes, const char* map, Totals& t) {
    constexpr int kStep = 97, w = 800, h = 480;
    // The map's projected extent: x - y over 4, x + y over 8 (world units).
    for (int top = -400; top < 8192 + 400; top += kStep) {
        for (int left = -16384 - 400; left < 16384 + 400; left += kStep) {
            RenderStats near_stats, all_stats;
            const auto near = build_display_list(world, shapes, View{left, top}, w, h, &near_stats);
            if (near.empty()) continue;  // nothing there: say nothing about the far cells
            const auto all = build_display_list(world, shapes, View{left, top}, w, h, &all_stats, nullptr, true);
            ++t.frames;
            if (near_stats.visited < all_stats.visited) ++t.near_frames;
            t.visited_near += near_stats.visited;
            t.visited_all += all_stats.visited;
            if (!same_list(near, all) && t.differing++ < 5) {
                std::fprintf(stderr, "  sweep of %s: view (%d,%d): %zu items near, %zu in all\n", map, left, top,
                             near.size(), all.size());
            }
        }
    }
}

void play(const std::filesystem::path& data) {
    const auto archive = FlexArchive::open(data / "STATIC/SHAPES.FLX");
    ShapeCache shapes(archive, ShapeLayout::World);
    check(shapes.reach() != nullptr, "an archive in memory gives a reach bound");

    Game game(data, std::filesystem::temp_directory_path() / "crusader-display-list");
    game.config().impact_marks = game.config().blood_marks = game.config().casings = true;
    game.new_game(0);
    Surface8 screen(800, 480);
    Totals t;
    auto step = [&](const char* phase) {
        game.update(kFrameUs);
        game.render(screen);
        compare(game, shapes, 800, 480, phase, t);
        compare(game, shapes, 640, 480, phase, t);
    };
    for (int i = 0; i < 3600 && !(game.mouse_in_play() && game.world().current_map() > 0); ++i) {
        if (i % 30 == 15) game.key(0x01, false, true, 0, 0x1B);
        if (i % 30 == 16) game.key(0x01, false, false, 0);
        step("before play");
    }
    const int first_map = game.world().current_map();
    check(first_map > 0, "the game reaches the first map");
    game.key(0x48, true, true, 0);
    for (int i = 0; i < 300; ++i) {
        if (i % 40 == 0) game.key(0x39, false, true, 0, ' ');
        if (i % 40 == 1) game.key(0x39, false, false, 0);
        step("walking");
    }
    game.key(0x48, true, false, 0);
    for (int turn = 0; turn < 8; ++turn) {
        game.key(0x4B, true, true, 0);
        for (int i = 0; i < 8; ++i) step("turning");
        game.key(0x4B, true, false, 0);
        for (int i = 0; i < 40; ++i) {
            if (i % 20 == 0) game.key(0x39, false, true, 0, ' ');
            if (i % 20 == 1) game.key(0x39, false, false, 0);
            step("firing");
        }
    }
    Totals swept;
    sweep(game.world(), shapes, "the first map", swept);
    for (const int map : {first_map == 2 ? 3 : 2, first_map}) {
        const auto at = landing(data, map);
        check(at.has_value(), "the map has an item to land on");
        if (!at) return;
        game.world().teleport(1, at->x, at->y, at->z, map);
        for (int i = 0; i < 200; ++i) step("another map");
        if (map != first_map) sweep(game.world(), shapes, "the other map", swept);
    }
    std::printf("sweep: %d views with items, differing %d; items visited per list %.0f near, %.0f all\n",
                swept.frames, swept.differing, static_cast<double>(swept.visited_near) / swept.frames,
                static_cast<double>(swept.visited_all) / swept.frames);
    check(swept.differing == 0, "over whole maps, the list from the cells near the view is the list from every item");
    check(game.world().items_in_grid(), "every item stayed inside the grid");
    std::printf("lists %d (%d from the cells near the view), differing %d; items visited per list %.0f near, %.0f all\n",
                t.frames, t.near_frames, t.differing, static_cast<double>(t.visited_near) / t.frames,
                static_cast<double>(t.visited_all) / t.frames);
    check(t.differing == 0, "the list from the cells near the view is the list from every item");
    check(t.near_frames > t.frames / 2, "most lists come from the cells near the view");
}

// The scenery animation, which only animates items partly on screen, finds
// them through the cells near the view and the NPC slots: two games with the
// same input, one of them without a reach bound (so it visits every item),
// must draw the same frames and draw the same random numbers.
void scenery(const std::filesystem::path& data) {
    Game near_game(data, std::filesystem::temp_directory_path() / "crusader-scenery-near");
    Game all_game(data, std::filesystem::temp_directory_path() / "crusader-scenery-all");
    auto both = [&](auto&& fn) {
        fn(near_game);
        fn(all_game);
    };
    both([](Game& g) { g.new_game(0); });
    all_game.world().set_reach_lookup([] { return static_cast<const ShapeReach*>(nullptr); });
    Surface8 a(800, 480), b(800, 480);
    int frames = 0, differing = 0;
    auto step = [&] {
        both([](Game& g) { g.update(kFrameUs); });
        near_game.render(a);
        all_game.render(b);
        ++frames;
        if ((a.pixels != b.pixels ||
             near_game.world().state.random_state != all_game.world().state.random_state) &&
            differing++ < 5) {
            std::fprintf(stderr, "  scenery: frame %d differs\n", frames);
        }
    };
    for (int i = 0; i < 3600 && !(near_game.mouse_in_play() && near_game.world().current_map() > 0); ++i) {
        if (i % 30 == 15) both([](Game& g) { g.key(0x01, false, true, 0, 0x1B); });
        if (i % 30 == 16) both([](Game& g) { g.key(0x01, false, false, 0); });
        step();
    }
    check(near_game.world().reach() != nullptr && all_game.world().reach() == nullptr,
          "one game has a reach bound, the other none");
    both([](Game& g) { g.key(0x48, true, true, 0); });
    for (int i = 0; i < 600; ++i) step();
    const int first_map = near_game.world().current_map();
    for (const int map : {first_map == 2 ? 3 : 2, first_map}) {
        const auto at = landing(data, map);
        if (!at) break;
        both([&](Game& g) { g.world().teleport(1, at->x, at->y, at->z, map); });
        for (int i = 0; i < 300; ++i) step();
    }
    std::printf("scenery: %d frames, differing %d\n", frames, differing);
    check(differing == 0, "scenery from the cells near the view animates as from every item");
}

void guards(const std::filesystem::path& data) {
    const auto shapes_flx = data / "STATIC/SHAPES.FLX";
    const auto size = std::filesystem::file_size(shapes_flx);
    const auto path = std::filesystem::temp_directory_path() / "crusader-display-list.reach";
    const ShapeReach small{1, 1, 1, 1};
    write_reach_file(path, small, size);
    check(read_reach_file(path, size) == small, "a reach file reads back");
    check(!read_reach_file(path, size + 1), "a reach file made for another archive is refused");
    std::filesystem::remove(path);

    FlexFile file = FlexFile::open(shapes_flx);
    ShapeCache streamed(file, ShapeLayout::World);
    check(streamed.reach() == nullptr, "a FlexFile gives no reach bound by itself");
    streamed.set_reach(small);
    check(streamed.reach() != nullptr, "a reach bound given is used");
    streamed.get(1);  // the player's shape reaches further than a pixel
    check(streamed.reach() == nullptr, "a bound a loaded shape exceeds is withdrawn");
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path data = argc > 1 ? argv[1] : "data";
    if (!std::filesystem::exists(data / "STATIC/SHAPES.FLX")) {
        std::printf("display_list_tests: no game data at %s, skipped\n", data.string().c_str());
        return 0;
    }
    try {
        play(data);
        scenery(data);
        guards(data);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        ++failures;
    }
    std::printf(failures ? "%d failure(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
