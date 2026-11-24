// Checks that drawing straight into a half-resolution screen (Surface8::half)
// gives exactly the full-size picture with every other pixel of every other
// line kept: two games get the same input, frame by frame, one drawn at
// 800 x 480, one at half resolution, through the movies and menus at the
// start (drawn full size, then halved), walking, firing (casings, impact
// marks), the autosave icon and a visit to another map. Needs the original
// data (data/, not in the repository) but no saved games: runs locally only.
// Then a computer screen with Game::set_overlay_zoom on the half-resolution
// game: the computer pixel for pixel over the halved scene.
// Usage: half_render_tests <data_dir> [png_dir]   (skips with exit code 0 when
// data is absent; png_dir gets the zoomed computer screen as shown)

#include "core/flex.hpp"
#include "core/png_writer.hpp"
#include "engine/ui/load_screen.hpp"
#include "engine/ui/main_menu.hpp"
#include "engine/world/world.hpp"
#include "game/game.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
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
    std::filesystem::path data;
    Game full_game, half_game;
    Surface8 full{kWidth, kHeight};
    Surface8 half = Surface8::half(kWidth, kHeight);
    Surface8 expected = Surface8::half(kWidth, kHeight);
    int frame = 0, differing = 0, in_play = 0;

    explicit Pair(const std::filesystem::path& data)
        : data(data),
          full_game(data, std::filesystem::temp_directory_path() / "crusader-half-render-full"),
          half_game(data, std::filesystem::temp_directory_path() / "crusader-half-render-half") {
        full.set_shown_halved(true);  // halved below: its menus' outlines drawn as the half game's
    }

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

// The computer screen with overlay zoom: the half game's stored picture is
// the full one halved, but for the computer's rectangle, which is the full
// picture's pixel for pixel, centred.
void zoomed_computer(Pair& p, const std::filesystem::path& png_dir) {
    // Back in play first: a movie or message at the end of the run above is skipped.
    for (int i = 0; i < 600 && !(p.full_game.mouse_in_play() && p.half_game.mouse_in_play()); ++i) {
        if (i % 30 == 15) p.both([](Game& g) { g.key(0x01, false, true, 0, 0x1B); });
        if (i % 30 == 16) p.both([](Game& g) { g.key(0x01, false, false, 0); });
        p.step("back to play");
    }
    check(p.full_game.mouse_in_play() && p.half_game.mouse_in_play(), "both games back in play");
    p.half_game.set_overlay_zoom(true);
    const std::string text = "OVERLAY ZOOM CHECK^THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG";
    p.both([&](Game& g) { g.read_computer(text); });
    for (int i = 0; i < 200; ++i) p.both([](Game& g) { g.update(kFrameUs); });
    p.full_game.render(p.full);
    p.half_game.render(p.half);
    keep_every_other_pixel(p.full, p.expected);
    if (!png_dir.empty()) {
        Surface8 shown(p.half.stored_width(), p.half.stored_height());
        shown.pixels = p.half.pixels;
        // The game palette: the game's own may be faded at this point.
        write_png(png_dir / "computer_zoomed.png", shown, Palette::load(p.data / "STATIC/GAMEPAL.PAL"));
    }
    // The computer (GUMPS.FLX shape 0x1E, frames 0 and 1) is 252 x 221 at
    // (274, 129) of the 800 x 480 picture: shown at (74, 9) of the 400 x 240 one.
    const int sw = p.half.stored_width(), sh = p.half.stored_height();
    int inside = 0, outside = 0, wrong = 0;
    for (int y = 0; y < sh; ++y) {
        for (int x = 0; x < sw; ++x) {
            const std::uint8_t got = p.half.row(y)[x];
            const bool in = x >= 74 && x < 74 + 252 && y >= 9 && y < 9 + 221;
            const std::uint8_t want = in ? p.full.row(y + 129 - 9)[x + 274 - 74]
                                         : p.expected.row(y)[x];
            ++(in ? inside : outside);
            if (got != want) ++wrong;
        }
    }
    std::printf("zoomed computer: %d pixels at full size, %d halved, %d wrong\n", inside, outside, wrong);
    check(wrong == 0, "the zoomed computer is the full picture's, the rest halved");
}

// The difficulty screen on a half-resolution screen: the outline of the
// selected level shows all four sides whatever the parity of its edges
// (level 2's top is on an odd line, the left and right edges on odd columns).
void difficulty_outline(const std::filesystem::path& data, const std::filesystem::path& png_dir) {
    Game g(data, std::filesystem::temp_directory_path() / "crusader-half-render-difficulty");
    Surface8 half = Surface8::half(kWidth, kHeight);
    const auto press = [&](std::uint16_t scan, bool ext, std::uint16_t ch = 0) {
        g.key(scan, ext, true, 0, ch);
        g.update(kFrameUs);
        g.key(scan, ext, false, 0);
        g.update(kFrameUs);
    };
    g.open_menu();
    g.update(kFrameUs);
    press(0x1C, false, 0x0D);  // new game: the difficulty screen
    const DifficultyScreen* ds = g.menu() ? g.menu()->difficulty_screen() : nullptr;
    check(ds != nullptr, "the difficulty screen opens");
    if (!ds) return;
    press(0x48, true);  // up: level 2
    press(0x48, true);  // up: level 1
    const int ox = (kWidth - 640) / 2, oy = (kHeight - 480) / 2;
    const auto kept = [&](int x, int y) { return half.row((oy + y) / 2)[(ox + x) / 2]; };
    for (int level = 1; level <= DifficultyScreen::kLevels; ++level) {
        check(ds->selected() == level, "the level selected");
        g.render(half);
        const DifficultyScreen::Rect& r = ds->button(level);
        const int mx = (r.x0 + r.x1) / 2 & ~1, my = (r.y0 + r.y1) / 2 & ~1;
        const auto even = [](int v, int inward) { return v & 1 ? v + inward : v; };
        const std::uint8_t c = DifficultyScreen::kOutlineColour;
        const bool top = kept(mx, even(r.y0, 1)) == c, bottom = kept(mx, even(r.y1, -1)) == c;
        const bool left = kept(even(r.x0, 1), my) == c, right = kept(even(r.x1, -1), my) == c;
        std::printf("difficulty level %d outlined at half resolution: top %d bottom %d left %d right %d\n", level, top,
                    bottom, left, right);
        check(top && bottom && left && right, "all four sides of the selected level's outline shown");
        if (level == 2 && !png_dir.empty()) {
            Surface8 shown(half.stored_width(), half.stored_height());
            shown.pixels = half.pixels;
            write_png(png_dir / "difficulty_halved.png", shown, g.palette());
        }
        if (level < DifficultyScreen::kLevels) press(0x50, true);  // down
    }
}

void zoomed_save_screen(Pair& p, const std::filesystem::path& png_dir);

// The 3DS layout of the load and save screens (six save slots): with
// overlay zoom, the whole screen (417 x 240) fits the 400 x 240 top screen
// at full size, cut by 8 pixels each side, whatever slot is selected.
int zoomed_load_screen_wrong(const LoadScreen& load, const Surface8& full, const Surface8& half) {
    const LoadScreen::Rect area = load.area();
    const int sw = half.stored_width(), sh = half.stored_height();
    const int ox = (kWidth - 640) / 2, oy = (kHeight - 480) / 2;
    check(area.y1 - area.y0 + 1 == sh, "six slots and the autosave fit 240 rows");
    const int x0 = ox + area.x0 + (area.x1 - area.x0 + 1 - sw) / 2, y0 = oy + area.y0;
    int wrong = 0;
    for (int y = 0; y < sh; ++y) {
        for (int x = 0; x < sw; ++x) wrong += half.row(y)[x] != full.row(y0 + y)[x0 + x];
    }
    return wrong;
}

void write_shown(const Surface8& half, const Palette& palette, const std::filesystem::path& png) {
    if (png.parent_path().empty()) return;
    Surface8 picture(half.stored_width(), half.stored_height());
    picture.pixels = half.pixels;
    write_png(png, picture, palette);
}

// The menu's load screen: the autosave first, then slots 1 to 6.
void zoomed_menu_load_screen(const std::filesystem::path& data, const std::filesystem::path& png_dir) {
    Game g(data, std::filesystem::temp_directory_path() / "crusader-half-render-load");
    g.set_overlay_zoom(true);
    g.set_save_slots(6);
    Surface8 half = Surface8::half(kWidth, kHeight);
    const auto press = [&](std::uint16_t scan, bool ext, std::uint16_t ch = 0) {
        g.key(scan, ext, true, 0, ch);
        g.update(kFrameUs);
        g.key(scan, ext, false, 0);
        g.update(kFrameUs);
    };
    g.open_menu();
    g.update(kFrameUs);
    while (g.menu() && g.menu()->selected() != MainMenu::kLoad) press(0x50, true);  // down to "load game"
    press(0x1C, false, 0x0D);
    const LoadScreen* load = g.menu() ? g.menu()->load_screen() : nullptr;
    check(load != nullptr, "the menu's load screen opens");
    if (!load) return;
    check(load->slot_rect(LoadScreen::kSlots - 1).y0 < load->slot_rect(0).y0, "the autosave above slot 1");
    press(0x48, true);  // up: the autosave
    check(load->slot() == LoadScreen::kSlots - 1, "the first row is the autosave");
    Surface8 full(kWidth, kHeight);
    full.set_shown_halved(true);
    for (int row = 0; row < 7; ++row) {
        if (row > 0) press(0x50, true);  // down
        g.render(full);
        g.render(half);
        const int wrong = zoomed_load_screen_wrong(*load, full, half);
        if (wrong) std::printf("zoomed menu load screen, row %d: %d pixels wrong\n", row, wrong);
        check(wrong == 0, "the menu's load screen shown at full size, unmoved");
        if (row == 0 && !png_dir.empty()) write_shown(half, g.palette(), png_dir / "load_screen_menu.png");
    }
    check(load->slot() == 5, "the last row is slot 6");
    press(0x50, true);
    check(load->slot() == 5, "no row under slot 6");
    std::printf("zoomed menu load screen checked\n");
}

void run(const std::filesystem::path& data, const std::filesystem::path& png_dir) {
    difficulty_outline(data, png_dir);
    zoomed_menu_load_screen(data, png_dir);
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
    zoomed_save_screen(p, png_dir);
    zoomed_computer(p, png_dir);
}

// The save screen (F9) with overlay zoom, six slots and auto save names, as
// on the 3DS: shown whole at full size; choosing a slot proposes the dated name.
void zoomed_save_screen(Pair& p, const std::filesystem::path& png_dir) {
    // Back in play first: a movie or message at the end of the run is skipped.
    for (int i = 0; i < 600 && !(p.full_game.mouse_in_play() && p.half_game.mouse_in_play()); ++i) {
        if (i % 30 == 15) p.both([](Game& g) { g.key(0x01, false, true, 0, 0x1B); });
        if (i % 30 == 16) p.both([](Game& g) { g.key(0x01, false, false, 0); });
        p.step("back to play");
    }
    check(p.full_game.mouse_in_play() && p.half_game.mouse_in_play(), "both games back in play for the save screen");
    p.both([](Game& g) { g.set_save_slots(6); });
    p.half_game.set_auto_save_names(true);
    const auto press = [&](std::uint16_t scan, bool ext, std::uint16_t ch = 0) {
        p.both([&](Game& g) {
            g.key(scan, ext, true, 0, ch);
            g.update(kFrameUs);
            g.key(scan, ext, false, 0);
            g.update(kFrameUs);
        });
    };
    // The mission's start may hold the player still: F9 needs him free.
    p.both([](Game& g) { g.world().state.avatar_in_stasis = g.world().state.cru_stasis = false; });
    press(0x43, false);  // F9
    for (int i = 0; i < 60; ++i) p.both([](Game& g) { g.update(kFrameUs); });
    check(p.half_game.screen() && p.full_game.screen(), "F9 opens the save screen");
    // Before: the whole screen halved.
    p.half_game.set_overlay_zoom(false);
    p.half_game.render(p.half);
    write_shown(p.half, p.half_game.palette(), png_dir.empty() ? "" : png_dir / "save_screen_halved.png");
    p.half_game.set_overlay_zoom(true);
    // The save screen is a LoadScreen of the same layout as the menu's.
    ShapeCache& gumps = p.half_game.gumps();
    LoadScreen::Names names;
    int selected = 0;
    const LoadScreen layout(gumps, p.half_game.fonts(), names, selected, LoadScreen::Mode::kSave, 6);
    for (int slot = 0; slot < 6; ++slot) {
        if (slot > 0) press(0x50, true);  // down
        p.full_game.render(p.full);
        p.half_game.render(p.half);
        const int wrong = zoomed_load_screen_wrong(layout, p.full, p.half);
        if (wrong) std::printf("zoomed save screen, slot %d: %d pixels wrong\n", slot, wrong);
        check(wrong == 0, "the save screen shown at full size, unmoved");
        if (slot == 0) write_shown(p.half, p.half_game.palette(), png_dir.empty() ? "" : png_dir / "save_screen.png");
    }
    press(0x1C, false, 0x0D);  // Enter: the dated name proposed
    p.half_game.render(p.half);
    write_shown(p.half, p.half_game.palette(), png_dir.empty() ? "" : png_dir / "save_screen_auto_name.png");
    press(0x01, false, 0x1B);  // Escape: the slot left as it was
    press(0x01, false, 0x1B);  // and the screen closed
    p.both([](Game& g) { g.set_save_slots(LoadScreen::kSaveSlots); });
    std::printf("zoomed save screen checked\n");
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path data = argc > 1 ? argv[1] : "data";
    if (!std::filesystem::exists(data / "STATIC/SHAPES.FLX")) {
        std::printf("half_render_tests: no game data at %s, skipped\n", data.string().c_str());
        return 0;
    }
    try {
        run(data, argc > 2 ? argv[2] : "");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        ++failures;
    }
    std::printf(failures ? "%d failure(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
