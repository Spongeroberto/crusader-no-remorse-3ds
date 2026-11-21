// Checks the autosave written in the background (Game::set_background_saves):
// its file is the one a save written during the frame gives, every save
// operation waits for it, and the game loads it. Needs the original data
// (data/, not in the repository) but no saved games: runs locally only.
// Usage: background_save_tests <data_dir>   (skips with exit code 0 when data is absent)

#include "core/flex.hpp"
#include "engine/world/world.hpp"
#include "game/game.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>

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

// The two saves hold the same game: every entry but the slot's name (9).
bool same_game(const std::filesystem::path& a, const std::filesystem::path& b) {
    const auto x = FlexArchive::open(a), y = FlexArchive::open(b);
    if (x.count() != y.count()) return false;
    for (std::size_t i = 0; i < x.count(); ++i) {
        if (i == 9) continue;
        const auto ex = x.entry(i), ey = y.entry(i);
        if (!std::equal(ex.begin(), ex.end(), ey.begin(), ey.end())) return false;
    }
    return true;
}

void run(const std::filesystem::path& data) {
    const auto saves = std::filesystem::temp_directory_path() / "crusader-background-save";
    std::filesystem::remove_all(saves);
    Game game(data, saves);
    game.new_game(0);
    // To play, as the bench does: Escape, held for one frame, skips the movies.
    for (int i = 0; i < 3600 && !(game.mouse_in_play() && game.world().current_map() > 0); ++i) {
        if (i % 30 == 15) game.key(0x01, false, true, 0, 0x1B);
        if (i % 30 == 16) game.key(0x01, false, false, 0);
        game.update(kFrameUs);
    }
    for (int i = 0; i < 120; ++i) game.update(kFrameUs);
    check(game.mouse_in_play(), "in play");

    const auto slot = [&](int n) { return saves / ("SAVEGAME." + std::to_string(n)); };
    game.save_game(5);
    game.set_background_saves(true);
    game.autosave();
    check(game.autosave_icon_shown(), "the autosave ran");
    game.finish_saves();
    check(std::filesystem::exists(slot(Game::kAutoSave)), "the autosave's file is written");
    check(same_game(slot(5), slot(Game::kAutoSave)), "the background autosave holds the game a save does");
    check(!std::filesystem::exists(saves / "SAVEGAME.12.tmp"), "no temporary file left");

    // Loading right after an autosave, without waiting: the load waits.
    for (int i = 0; i < 60; ++i) game.update(kFrameUs);
    game.save_game(6);
    game.autosave();
    game.load_save(Game::kAutoSave);
    check(same_game(slot(6), slot(Game::kAutoSave)), "the second autosave replaced the first");
    check(game.world().get(1) != nullptr, "the autosave loads");
    for (int i = 0; i < 60; ++i) game.update(kFrameUs);

    // Switched off: what is queued is written first.
    game.autosave();
    game.set_background_saves(false);
    check(std::filesystem::exists(slot(Game::kAutoSave)), "switching off writes the queue");
    std::filesystem::remove_all(saves);
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path data = argc > 1 ? argv[1] : "data";
    if (!std::filesystem::exists(data / "STATIC/SHAPES.FLX")) {
        std::printf("background_save_tests: no game data at %s, skipped\n", data.string().c_str());
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
