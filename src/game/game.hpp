#pragma once
// The running game, without platform code: data, world, process kernel and
// usecode machine (what CRUSYS.C sets up before entering the main loop).
//
// The original main loop (KERNEL.C 31CF, called from the entry point at
// 1048:0079 with a period of 0) runs kernel slices back to back; time comes
// from the 60 Hz system timer and the timed processes, and the display is a
// process too. Here the platform layer calls update() with the real elapsed
// time, then render(), once per presented frame.

#include "core/anim_dat.hpp"
#include "core/background_writer.hpp"
#include "core/data_files.hpp"
#include "core/game_config.hpp"
#include "core/dtable.hpp"
#include "core/font.hpp"
#include "core/xform.hpp"
#include "core/flex.hpp"
#include "core/flex_file.hpp"
#include "core/palette.hpp"
#include "core/surface.hpp"
#include "core/trig.hpp"
#include "core/typeflags.hpp"
#include "engine/audio/audio.hpp"
#include "engine/input/keyboard.hpp"
#include "engine/input/mouse_queue.hpp"
#include "engine/kernel.hpp"
#include "engine/ui/help_screen.hpp"
#include "engine/ui/main_menu.hpp"
#include "engine/ui/message_gump.hpp"
#include "engine/ui/modal_gump.hpp"
#include "engine/ui/movie_player.hpp"
#include "engine/ui/mouse_cursor.hpp"
#include "engine/ui/pickup_area.hpp"
#include "engine/ui/status_bar.hpp"
#include "engine/usecode/uc_machine.hpp"
#include "engine/usecode/usecode_file.hpp"
#include "engine/world/avatar_mover.hpp"
#include "engine/world/combat_control.hpp"
#include "core/weapon_overlay.hpp"
#include "engine/world/barrel_leaks.hpp"
#include "engine/world/glass_debris.hpp"
#include "engine/world/casings.hpp"
#include "engine/world/damage_marks.hpp"
#include "engine/world/lighting.hpp"
#include "engine/world/render.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_mouse.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace crusader {

// A data file of the game: the mod folder's copy when there is one (the
// level editor writes STATIC/FIXED.DAT, STATIC/GLOB.FLX... there), else the
// data folder's.
// @orig none: mod folders are a port addition
std::filesystem::path game_file(const std::filesystem::path& data, const std::filesystem::path& mod,
                                const std::string& relative);

class Game {
public:
    static constexpr int kScreenWidth = 640;
    static constexpr int kScreenHeight = 480;
    static constexpr int kQuickSave = 13;  // SAVEGAME.13, slot 12 (F4 / F5)
    static constexpr int kAutoSave = 12;   // SAVEGAME.12, port addition (F6): the autosave

    // `saves`: where the saved games are read and written, data/SAVEGAME
    // when empty (the tests use a copy of the original saves). `mod`: a
    // folder made by the level editor; its files replace those of the data
    // (game_file), and its games are saved in mod/SAVEGAME.
    // `stream_shapes` (port only, for consoles short of memory): SHAPES.FLX
    // is not read whole; the shapes a map uses are read when it loads and
    // the others freed (prepare_shapes). The game plays the same either way.
    explicit Game(const std::filesystem::path& data, const std::filesystem::path& saves = {},
                  const std::filesystem::path& mod = {}, bool stream_shapes = false);
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    // Loads SAVEGAME/SAVEGAME.<slot>, puts the camera on the player and shows
    // the status bar (end of GAMEPAL_1028_0000).
    void load_save(int slot);
    // Same, from any file (the tests keep saves under names telling their bug).
    void load_save_file(const std::filesystem::path& path);

    // The world's shapes: how many are held and how they were read.
    ShapeCache::Stats shape_stats() const { return shapes_.stats(); }
    // Port only, with stream_shapes: called just before a map's shapes are
    // read when there are many (kLoadingNoticeShapes or more), so a console
    // can show that it is loading for the second or two the reads take. The
    // game shows nothing itself: the reads happen within update().
    static constexpr std::size_t kLoadingNoticeShapes = 32;
    void set_loading_notice(std::function<void()> shown) { loading_notice_ = std::move(shown); }

    // Port only, for measuring on slow hardware: time spent in the main
    // phases of update and render, in microseconds, summed over every frame
    // since the clock was set. Nothing is measured without a clock.
    struct Profile {
        double kernel_us = 0;   // processes and usecode (Kernel::run_slice)
        double scenery_us = 0;  // animate_scenery
        double audio_us = 0;    // mixing, when no device takes the sound
        double clear_us = 0;    // render_view: the screen cleared
        double list_us = 0;     // build_display_list
        double prepare_us = 0;  // casings and marks prepared
        double draw_us = 0;     // the display list drawn
        double overlay_us = 0;  // sprites, selection, status bar, pickups
    };
    void set_profile_clock(std::function<double()> clock_us) { profile_clock_ = std::move(clock_us); }
    const Profile& profile() const { return profile_; }
    void reset_profile() { profile_ = {}; }

    // Game_save (CRUSYS_1058_0115): writes SAVEGAME/SAVEGAME.<slot> under
    // the name of that slot (save_names(), entry 9). Only the entries the
    // port holds are written: 9, 90, 91, the item table and its lists (0,
    // 1), the NPC table (10), the player block (20), the tracked items (50
    // to 52), the usecode globals (60) and the music (70, 71). The sprite
    // manager (30 to 33), the YAMM memory (40, 41) and the kernel's processes
    // (100 and up) are not, so the original cannot load these saves yet.
    void save_game(int slot);
    // CRUSYS_1058_0528: the name of save slot `slot` (1-based), at most 31
    // characters; the save screen sets it before saving.
    void set_save_name(int slot, const std::string& name);
    const LoadScreen::Names& save_names() const { return save_names_; }
    // The end of the save screen (FUN_1008_041a): the name, the disk space
    // check, then save_game.
    void save_named(int slot, const std::string& name);
    // save_game, a failure shown in the message box; false then.
    bool save_or_report(int slot);

    // Port addition (GameConfig::autosave): saves the game in slot kAutoSave
    // and shows the floppy disk for a moment. Does nothing when switched off,
    // in a menu, a screen or a movie, or when the player is not the controlled
    // NPC. It happens when a keycard is picked up, a door opens with one or
    // with a code, a chest opens with a code (the keypad matched), a mission
    // starts and a movie ends; the last two wait until the player may act
    // again (request_autosave).
    void autosave();
    void request_autosave();
    // The floppy disk is on screen.
    bool autosave_icon_shown() const { return autosave_icon_us_ > 0; }
    // Port only, for slow media (the 3DS SD card): the autosave's file is
    // written on a thread of its own instead of during the frame. The game
    // is still saved, into memory, when autosave() is called; anything that
    // reads or writes saves first waits for that file (finish_saves).
    void set_background_saves(bool on);
    // Returns once a save written in the background is on disk.
    void finish_saves() {
        if (save_writer_) save_writer_->wait();
    }

    // Port only: where the saved games are read and written (data/SAVEGAME
    // by default); the slot names are read again from there.
    void set_save_dir(const std::filesystem::path& dir);




    // A new game (GAMEPAL_1028_0000, menu result 0): everything is cleared,
    // the usecode globals come from USECODE/UNKDS.DAT (FLAG_1410_0051), the
    // player is made again (NPC_10f0_000c / 385c: item 1, shape 1, 150 hit
    // points; difficulty 3 unless 1..4), with the datalink (0x4D4, through
    // MainActor::I_addItemCru) and a container (0x598) in his inventory.
    // Then a teleport to egg 0x1E of map 1 (SHAPHAND_1098_011f) loads the
    // first mission when the kernel runs.
    void new_game(int difficulty);
    // Where new_game puts the player: the egg (teleport egg of frame 1 whose
    // quality low byte is `egg`) of a map. The first mission's by default;
    // the level editor starts the map it edits (--map, --egg).
    void set_start(int map, std::uint16_t egg) {
        start_map_ = map;
        start_egg_ = egg;
    }

    // Advances the clock by `elapsed_us` and runs one kernel slice. While the
    // menu is open, only the processes that run at any level do.
    void update(std::uint32_t elapsed_us);

    // Draws the view (the original's screen origin, see World::set_camera),
    // then the status bar, or the menu; then the mouse cursor if shown.
    // `screen` is 640 x 480, or larger for the native resolution: the view then
    // shows more around the camera, and the menus, screens, films and cursor
    // stay 640 x 480 in the middle (see ui_x / ui_y for the mouse).
    void render(Surface8& screen, RenderStats* stats = nullptr);
    // Port addition (GameConfig::lighting): the screen just rendered, with the
    // world lit in 3D, in `argb` (0xAARRGGBB). False when there is nothing to
    // light (option off, cutscene, menu): show `screen` as it is.
    bool render_lit(const Surface8& screen, std::vector<std::uint32_t>& argb);
    // The GPU that shades the lighting (null: the processor). Kept by the caller.
    void set_lighting_gpu(LightingGpu* gpu);
    // The 3D lighting (null until first switched on), for the tests.
    const WorldLighting* lighting() const { return lighting_.get(); }
    const BarrelLeaks& barrel_leaks() const { return barrel_leaks_; }
    const GlassDebris& glass_debris() const { return glass_debris_; }
    // Where the 640 x 480 area starts in the last rendered screen; the mouse
    // coordinates the game takes are relative to it.
    int ui_x() const { return ui_x_; }
    int ui_y() const { return ui_y_; }

    // Moves the camera by (dx, dy) usecode units at its current height.
    void move_camera(std::int32_t dx, std::int32_t dy);

    // A key went down or up (PC set-1 scancode, E0 prefix, BIOS shift state),
    // as the keyboard interrupt handler sees it.
    void key(std::uint8_t scancode, bool extended, bool pressed, std::uint16_t flags, std::uint8_t ascii = 0);
    const Keyboard& keyboard() const { return keyboard_; }

    // Mouse driver events, in screen coordinates (MouseCtl_on_driver_event);
    // button 1 is the left one, 2 the right one. In play they go through the
    // mouse queue (mouse_queue.hpp) to the game view (world_mouse.hpp);
    // the menus and screens take the left button at once.
    void mouse_move(int x, int y);
    void mouse_button(bool pressed, int x, int y) { mouse_button(1, pressed, x, y); }
    void mouse_button(int button, bool pressed, int x, int y);
    const MouseCursor& cursor() const { return cursor_; }
    // Port addition: a platform without a pointer (the 3DS) never shows the
    // cursor; the game keeps its state, only the drawing is skipped.
    void set_cursor_drawn(bool drawn) { cursor_drawn_ = drawn; }
    // Port addition: a platform that shows the equipment on a second screen
    // (the 3DS) leaves the status bar out of the view.
    void set_status_bar_drawn(bool drawn) { status_bar_drawn_ = drawn; }
    // Port addition: a platform closed from its own system menu (the 3DS's
    // HOME button) leaves "quit" out of the main menu.
    void set_menu_quit_shown(bool shown) { menu_quit_shown_ = shown; }
    // Port addition: on a half-resolution screen (Surface8::half), a screen
    // drawn over the view that reports its area (the computer, the keypad,
    // the dialogs) is shown at full size over the halved scene, when it fits
    // in the stored picture, so that its text stays readable (the 3DS). The
    // items just picked up (top left) are drawn at full size too.
    void set_overlay_zoom(bool zoom) { overlay_zoom_ = zoom; }
    // Controller screen (options): it waits for a button press to bind, which
    // the platform gives with pad_button() instead of sending its key.
    bool waiting_for_pad() const;
    void pad_button(int button);
    // Installs the platform's rumble handler for gameplay events.
    void set_vibration_handler(std::function<void(std::uint16_t, std::uint16_t, std::uint32_t)> handler) {
        vibration_handler_ = std::move(handler);
    }
    void preview_vibration();
    // In play (no menu, screen, message box or movie), the game view puts the
    // mouse back at (320, 200) after each move: only relative motion counts.
    // Directional aiming: the right stick's direction on the screen (degrees,
    // counter-clockwise from east) while it is pushed, applied by update().
    void set_aim_stick(bool active, double screen_deg) {
        aim_stick_ = active;
        aim_stick_deg_ = screen_deg;
    }
    bool mouse_in_play() const { return !modal() && !movie_; }
    // The controlled NPC's facing as a screen direction (degrees,
    // counter-clockwise from east), for set_aim_stick to aim straight ahead.
    double facing_screen_deg() const;
    // The item selection mode of the game view is on (item_selection.hpp):
    // there Enter uses or picks up the selected item, which U does not.
    bool selecting() const { return world_ && world_->state.selection.active; }
    // In play, the weapon (or item) in the player's equipment `slot` made
    // active directly (select_weapon / select_item). True when it is active.
    bool select_equipment(bool weapon, int slot);
    // Ends the aiming mode as a non-aiming action does (aim_after_do_anim):
    // the crosshair goes and the fire state is reset. For a hold-to-aim
    // button; the original has no key for it.
    void leave_aim();
    // A hold-to-aim button (World::hold_to_aim): a stopped run does not
    // enter the aiming mode by itself.
    void set_hold_to_aim(bool on) { hold_to_aim_ = on; }
    // True in play while the controlled NPC has its weapon out. The controller
    // then strafes with the left stick while the right stick aims.
    bool weapon_out() const;
    const WorldMouse& world_mouse() const { return *world_mouse_; }
    ShapeCache& shapes() { return shapes_; }
    ShapeCache& gumps() { return gumps_; }
    const XformTable& xform() const { return xform_; }

    // In-game menu (Escape, WORLD_13e8_10c3). Closing it with "quit" sets
    // quit_requested(); with a slot chosen in "load", that game is loaded at
    // the next update (CRUSYS_1058_02f1).
    void open_menu();
    const MainMenu* menu() const { return menu_.get(); }

    // Game::I_playCredits (intrinsic 0x095, 13E8:003F), the end of the game:
    // at the next update the music stops, the credits run, then the movies
    // of the start-up (T01, T02) and the start-up menu, with no game to go
    // back to (see play_end_credits in game.cpp).
    void play_credits() { end_credits_pending_ = true; }

    // Normal launch (Game_start, 1028:0000), with no game given on the
    // command line: the movies T01 and T02, then the start-up menu with no
    // game running ("back" and "save" do nothing), where "new game" and
    // "load" lead into a game.
    void start_up();

    // The engine's message box (message_gump.hpp, the WorldHooks::show_message
    // of the world): the game freezes as for the menu until a key, or a click
    // on "Ok", closes it. Nothing when one is already open.
    void show_message(const std::string& text);
    const MessageGump* message() const { return message_.get(); }

    // A screen opened by a function key of the game view (World_on_key):
    // help, options, load screen, the questions of F4 / F5 / Alt+X and the
    // notice after a quick load; or the computer screen of
    // ComputerGump::I_readComputer and the keypad of KeypadGump::I_showKeypad.
    // The game is frozen while it is open.
    const ModalGump* screen() const { return screen_.get(); }

    // A cutscene (the movie intrinsics, see movie_player.hpp): FLICS/<name>,
    // nothing when it does not exist. The kernel pass ends at once; until the
    // movie is over, update() plays it instead of running the kernel, and
    // render() shows it.
    void play_movie(const std::string& name, MovieKind kind);
    // Port: on a screen at half resolution (Surface8::half, the 3DS), a
    // cutscene (not an overlay), once its fade is over, fills the screen's
    // width, scaled from its own pixels, the top and bottom cut off (black
    // bands for one wider than the screen); its subtitles go over the bottom
    // of the picture. Off: the 640 x 480 screen, every other pixel kept.
    void set_movies_fill_screen(bool on) { movies_fill_screen_ = on; }
    const MoviePlayer* movie() const { return movie_.get(); }

    // ComputerGump::I_readComputer (computer_gump.hpp): the text on the
    // computer screen, in the menu mode, until it is closed. The calling
    // process goes on afterwards. Nothing when a screen is already open.
    void read_computer(const std::string& text);
    // KeypadGump::I_showKeypad (keypad_gump.hpp): the keypad for `code`, in
    // the menu mode, until it is closed; then process `pid` gets its result
    // (-1 cancelled, else the value entered) as the call's result. Nothing
    // (result 0) when a screen is already open.
    void show_keypad(std::int16_t code, Pid pid);
    // WeaselGump::I_showWeaselGump (weasel_gump.hpp): Weasel's shop for
    // `level`, in the menu mode, until it is closed; its movies are played
    // as cutscenes. Nothing when a screen is already open.
    void show_weasel(int level);
    bool quit_requested() const { return quit_; }

    World& world() { return *world_; }
    Kernel& kernel() { return *kernel_; }
    UCMachine& machine() { return *machine_; }
    // Palette on screen (the DAC): GAMEPAL.PAL in play, MISC.PAL in the menu,
    // changed by the palette fades.
    const Palette& palette() const { return world_->state.screen_palette; }
    const AnimDat& animations() const { return anims_; }
    const FontSet& fonts() const { return fonts_; }
    const StatusBar& status_bar() const { return status_bar_; }
    const CombatControl& combat() const { return *combat_; }
    // The sound system, null without SOUND.FLX.
    Audio* audio() { return audio_.get(); }
    // Without an audio device pulling the mixer (tests, runs without a
    // window), update() mixes the time that passed itself, so that sounds
    // end; with `capture`, the samples are kept.
    void set_audio_device(bool attached) { audio_device_ = attached; }
    void capture_sound(bool on) { capture_ = on; }
    const std::vector<std::int16_t>& captured_sound() const { return captured_; }
    // CRUSADER.CFG, read from the data directory at start-up. Closing the
    // options screen writes it to `path` (config_save); nothing is written
    // until a path is given.
    const GameConfig& config() const { return config_; }
    GameConfig& config() { return config_; }
    // Replaces it, the sound settings applied (tests).
    void set_config(GameConfig config);
    // Port addition: the biggest pixel size the window allows, in hundredths
    // (the enhancements window stops there).
    void set_view_zoom_limit(int limit_pct) { config_.view_zoom_limit_pct = limit_pct; }
    void set_config_file(std::filesystem::path path) { config_file_ = std::move(path); }
    // The two lines at the bottom of the main menu: the port's version (from
    // the build; empty hides the line) and the version of the game data
    // detected in CRUSADER.EXE ("CRUSADER:  No Remorse Vx.yy"), with its language.
    void set_port_version(std::string version) { port_version_ = std::move(version); }
    // Port addition, for consoles without a keyboard (--auto-save-names, given
    // by the PortMaster launcher): the save screens propose the date and time
    // as the name (LoadScreen::set_auto_name), confirmed with the pad's A.
    void set_auto_save_names(bool on) { auto_save_names_ = on; }
    // Port addition, for small screens: the load and save screens show this
    // many save slots (LoadScreen's compact layout below eleven).
    void set_save_slots(int slots) { save_slots_ = slots; }
    // Port addition: the date is 24 to 31 December (is_christmas), set by the
    // program from the clock: the main menu's Silencer wears a Christmas hat
    // until Ctrl+M turns the Christmas mode off (christmas_hat.hpp).
    void set_christmas(bool on) { christmas_ = on; }
    // CRUSYS_1050_093D: the "-laurie" argument enables the cheats (DS:08F0),
    // without turning them on (cheats.hpp).
    void enable_cheats() { world_->state.cheats.enabled = true; }
    const std::string& port_version() const { return port_version_; }
    // "" when CRUSADER.EXE is missing or has no version string.
    const std::string& data_version() const { return data_version_; }
    // "FR" or "US", "" when not recognised.
    const std::string& data_language() const { return data_language_; }
    // Frames that fired a weapon (each calls Item::I_fireWeapon).
    int shots_fired() const { return shots_fired_; }

private:
    // The save screens' name proposer (LoadScreen::set_auto_name): the local
    // date and time with set_auto_save_names, else none.
    std::function<std::string()> auto_save_name_function() const;
    void fire_frame(ObjId actor, int muzzle_a, int muzzle_b, int muzzle_z);
    void burn_in_fires();
    void vibrate(std::uint16_t low_frequency, std::uint16_t high_frequency, std::uint32_t duration_ms,
                 bool preview = false);
    void reset_world();
    // With streamed shapes: holds the shapes of `map` (its FIXED.DAT items
    // and the globs they expand to) and of every item in the world now (the
    // player, NPCs, what they carry), frees the others, and reads the missing
    // ones in a few large requests. A shape needed later is read on first use.
    void prepare_shapes(int map);
    // The usecode globals (0x42 bytes in this version), the rest cleared.
    void set_globals(std::span<const std::uint8_t> bytes);
    void on_key(const GameKeyEvent& e);
    void close_menu();
    void close_message();
    // World_on_key's function keys and Alt+X: true when `code` opened a screen.
    bool function_key(std::uint16_t code);
    // Runs `screen` as Gump_run_modal does; `closed` gets it once closed.
    void open_screen(std::unique_ptr<ModalGump> screen, std::function<void(const ModalGump&)> closed);
    void close_screen();
    // A question of the game view (13F8:0000) with its sound; `yes` runs when
    // it is answered 1, the menu mode being left otherwise.
    void ask(std::uint16_t shape, int sfx, std::function<void()> yes);
    // The notice (13F8:0383), the menu mode left when it closes.
    void notify(std::uint16_t shape);
    // World_enter_menu_mode / World_leave_menu_mode (13E8:0E7D / 0EF9).
    void enter_menu_mode();
    void leave_menu_mode();
    // The menu, a message box or a screen holds the game.
    bool modal() const { return menu_ || message_ || screen_; }
    void dispatch_mouse();
    // Palette and cursor of the menu's difficulty screen when it opens or closes.
    void follow_menu_screen();
    void apply_status_bar_mouse_range();
    void run_credits(std::uint32_t ticks_before);
    void play_end_credits();
    void open_menu(bool no_game);
    void start_movie(const std::string& name, MovieKind kind, Surface8 screen);
    void play_weasel_movie(const std::string& name);
    void next_start_up_movie();
    // Without an audio device, mixes the time that passed (see set_audio_device).
    void mix_elapsed(std::uint32_t elapsed_us);
    // The game process's pass (1038:01B1) and the animation of the scenery
    // it asks for (DS:2C52), unless a modal screen pauses the game.
    void animate_scenery();
    // The game view and the status bar, without the cursor. With `keep_view`
    // (a movie's background), under a modal screen or during a movie, the
    // view keeps its size: a screen of another size gets its centre.
    void render_view(Surface8& screen, RenderStats* stats = nullptr, bool keep_view = false);
    void zoom_overlay(Surface8& screen, const ModalGump::Area& area, const std::optional<ModalGump::Area>& focus) const;
    template <typename Draw>
    void draw_window(Surface8& screen, int x0, int y0, Draw&& draw);
    template <typename Draw>
    void draw_window_in_place(Surface8& screen, int x0, int y0, Draw&& draw);
    Surface8 window_;  // 640 x 480 scratch for draw_window
    Surface8 full_screen_;  // full-size scratch for a half-resolution screen outside play
    int ui_x_ = 0, ui_y_ = 0;
    void update_movie(std::uint32_t elapsed_us);
    void draw_movie_filling(Surface8& screen) const;
    void end_movie();
    void save_config();
    void draw_autosave_icon(Surface8& screen);


    // Where the data files are read: the language folder picked, the mod
    // folder, the data folder (data_files.hpp). Before the data it opens.
    DataFiles files_;
    // Data, kept open for the whole run.
    FlexArchive shapes_flex_;               // empty with streamed shapes
    std::optional<FlexFile> shapes_file_;   // with streamed shapes
    std::function<double()> profile_clock_;
    Profile profile_;
    FlexArchive globs_;
    FlexArchive fixed_;
    std::vector<ShapeInfo> typeflags_;
    TrigTables trig_;
    std::vector<std::uint8_t> damage_;
    AnimDat anims_;
    bool aim_stick_ = false;
    double aim_stick_deg_ = 0;
    bool hold_to_aim_ = false;
    WeaponOverlay weapon_overlay_;
    DamageMarks damage_marks_;  // port addition (GameConfig::impact_marks)
    Casings casings_;           // port addition (GameConfig::casings)
    BarrelLeaks barrel_leaks_;  // port addition (GameConfig::barrel_leaks)
    GlassDebris glass_debris_;  // port addition (GameConfig::glass_debris)
    std::uint32_t fire_smoke_tick_ = 0;  // the fires' smoke is made up to this tick
    // Port addition (GameConfig::lighting), made when first switched on.
    std::unique_ptr<WorldLighting> lighting_;
    LightingGpu* lighting_gpu_ = nullptr;
    Palette palette_;
    UsecodeFile usecode_;
    ShapeCache shapes_;
    FlexArchive gumps_flex_;
    ShapeCache gumps_;
    FontSet fonts_;
    XformTable xform_;
    dtable::NpcTypes npc_types_;
    StatusBar status_bar_;
    PickupArea pickup_area_;
    Palette menu_palette_;
    Palette difficulty_palette_;
    MouseCursor cursor_;
    bool cursor_drawn_ = true;
    bool status_bar_drawn_ = true;
    bool menu_quit_shown_ = true;
    bool overlay_zoom_ = false;
    std::filesystem::path data_;
    std::filesystem::path mod_;  // level editor files that replace the data's
    int start_map_ = 1;
    std::uint16_t start_egg_ = 0x1E;  // kTeleportStartEgg
    std::string port_version_;
    std::string data_version_;  // "1.08", "1.23", "1.01"
    std::string data_language_;
    std::span<const std::uint16_t> intrinsic_numbering_;  // of the usecode (intrinsic_numbering.hpp)
    std::filesystem::path save_dir_;  // data_/SAVEGAME unless set_save_dir
    std::unique_ptr<BackgroundWriter> save_writer_;  // set_background_saves
    // The saved game save_game writes, as bytes.
    std::vector<std::uint8_t> saved_game(int slot);
    GameConfig config_;
    std::filesystem::path config_file_;
    std::unique_ptr<Audio> audio_;  // kept across loads, as the sound driver
    std::function<void(std::uint16_t, std::uint16_t, std::uint32_t)> vibration_handler_;
    bool audio_device_ = false;
    bool credits_music_ = false;
    bool capture_ = false;
    std::vector<std::int16_t> captured_;
    std::uint64_t mixed_us_ = 0, mixed_samples_ = 0;

    // Rebuilt by each load, in this order (the machine refers to the kernel,
    // the intrinsics to the world).
    std::function<void()> loading_notice_;  // set_loading_notice
    std::unique_ptr<Kernel> kernel_;
    std::unique_ptr<UCMachine> machine_;
    std::unique_ptr<World> world_;

    Keyboard keyboard_;
    std::unique_ptr<AvatarMover> mover_;
    TimedProcess* key_daemon_ = nullptr;  // owned by the kernel
    CombatControl* combat_ = nullptr;     // owned by the kernel
    MouseQueue mouse_queue_;
    std::uint16_t mouse_buttons_ = 0;     // driver state: bit 0 left, bit 1 right
    std::unique_ptr<WorldMouse> world_mouse_;
    int shots_fired_ = 0;

    std::unique_ptr<MoviePlayer> movie_;
    // Player field +0xC2: the movie resumes the colour cycling when it ends
    // (DS:5FFE clear when it started).
    bool movie_resumes_cycle_ = true;
    bool movies_fill_screen_ = false;  // set_movies_fill_screen
    // Port addition (issue #24): the keys that stop a movie (Escape, Space,
    // Enter) already held when it started, ignored until they are released.
    std::array<bool, 3> movie_keys_held_at_start_{};
    // A movie of Weasel's shop: the game palette and the colour cycling come
    // back after it (13E0:0CD8).
    bool weasel_movie_ = false;
    bool auto_save_names_ = false;     // set_auto_save_names
    int save_slots_ = LoadScreen::kSaveSlots;  // set_save_slots
    bool christmas_ = false;           // set_christmas
    bool weasel_intro_played_ = false;  // DS:5ECF
    bool restoring_fast_area_ = false;  // load_save_file rebuilds the saved fast area
    // The game process's pass count (type 0x245, +0x40 / +0x44) and its value
    // at the last animation pass of the scenery (DS:2D3B), item_anim.hpp.
    std::uint32_t game_passes_ = 0;
    std::uint32_t animated_at_ = 0;
    // Sounds of the last movies: the mixer may still read the last one for a
    // buffer after it is stopped.
    std::deque<std::shared_ptr<const std::vector<std::int8_t>>> movie_sounds_;

    std::unique_ptr<MainMenu> menu_;
    Process* menu_process_ = nullptr;  // modal placeholder, owned by the kernel
    MainMenu::Shared menu_shared_;     // DS:5BE8, DS:0098
    LoadScreen::Names save_names_;     // DS:7964
    CreditsData credits_data_;
    HelpScreen::Pictures help_pictures_;
    bool autosave_pending_ = false;    // request_autosave, until the player may act
    std::uint32_t autosave_due_ = 0;   // the kernel tick from which it can run
    // A line of scripts that must end first (the keypad's, Process::lineage),
    // waited for until autosave_give_up_ at most.
    std::uint16_t autosave_lineage_ = 0;
    std::uint16_t last_lineage_ = 0;
    std::uint32_t autosave_give_up_ = 0;
    bool run_toggled_ = false;         // GameConfig::toggle_run: running is switched on
    std::uint32_t autosave_icon_us_ = 0;  // what is left of the floppy disk's display
    std::vector<std::uint8_t> floppy_;    // its palette indices (0xFF: transparent), built when first drawn
    int pending_load_ = 0;             // save to load at the next update (1-based)
    int pending_new_game_ = 0;         // difficulty of a new game to start at the next update
    bool end_credits_pending_ = false; // Game::I_playCredits: its process has not run yet
    bool end_credits_ = false;         // the credits running are the end of the game's
    std::vector<std::string> next_movies_;  // Game_start: movies to play after the current one
    std::unique_ptr<MessageGump> message_;
    Process* message_process_ = nullptr;  // modal placeholder, owned by the kernel
    std::unique_ptr<ModalGump> screen_;
    std::function<void(const ModalGump&)> screen_closed_;
    Process* screen_process_ = nullptr;   // modal placeholder, owned by the kernel
    std::uint16_t pending_notice_ = 0;    // notice to show after the pending load
    bool sprites_suspended_ = false;      // DS:2D8F: the screen sprites are not drawn
    bool difficulty_shown_ = false;    // the menu shows the difficulty screen
    bool quit_ = false;
    bool status_bar_shown_ = false;    // last state seen, for the mouse range
};

}  // namespace crusader
