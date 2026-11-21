// Crusader: No Remorse on a 3DS: the game's 800 x 480 view drawn at half
// resolution on the top screen, the player's equipment on the bottom one in
// play (engine/ui/inventory_panel.hpp), the controls in menus. Buttons send
// the original's keys, as the PC version's gamepad does (core/pad_config.hpp).
// There is no mouse: the cursor is never drawn and every menu, screen and
// question is answered with keys. In play a tap on the equipment picks a
// weapon or item. Data comes from romfs:/data when the
// CIA bundles it, else from sdmc:/3ds/crusader/data; saves and settings go to
// sdmc:/3ds/crusader. No sound yet.

#include "core/pad_config.hpp"
#include "engine/input/keyboard.hpp"
#include "engine/ui/inventory_panel.hpp"
#include "game/game.hpp"
#include "top_screen.hpp"

#include <3ds.h>

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>

// The picture is drawn by the CPU and needs only a few MiB the GPU can read;
// the rest of the 64 MiB goes to the ordinary heap.
extern "C" {
u32 __ctru_linear_heap_size = 4u << 20;
u32 __stacksize__ = 0x40000;  // as the CIA asks (app.rsf StackSize)
}

namespace {

using namespace crusader;

constexpr const char* kRoot = "sdmc:/3ds/crusader";

double now_us() { return static_cast<double>(svcGetSystemTick()) / (SYSCLOCK_ARM11 / 1'000'000.0); }

// The 3DS controls, each sending one of the original's keys as the PC
// version's gamepad does: the modifier key first, released last.
enum Control : int {
    kA, kB, kX, kY, kL, kR, kStart, kSelect,
    kPadUp, kPadDown, kPadLeft, kPadRight,  // the D-pad
    kControlCount,
};

const PadAction* action(int id) { return &kPadActions[static_cast<std::size_t>(id)]; }

// "Yes" to the questions: O, which the yes/no dialogs (ShapeDialog) and
// Weasel's shop take as "oui" / yes, and which is O on QWERTY, QWERTZ and
// AZERTY alike (unlike Y and J).
constexpr PadAction kYes = {"YES", 0x18, false, 'o'};

// Original keys with no PadAction (player_commands.hpp): E uses an energy
// cube, G picks up everything near, B sets off the detpacs.
constexpr PadAction kEnergyCube = {"ENERGY CUBE", 0x12, false, 'e'};
constexpr PadAction kPickUpAll = {"PICK UP ALL", 0x22, false, 'g'};
constexpr PadAction kDetonate = {"DETONATE", 0x30, false, 'b'};

// In play. The circle pad moves, and runs when pushed far out (see
// Controls::move and Controls::run). R held aims (Controls::aim); D-pad left /
// right step aside, or roll on a double tap (Controls::side_tap). Y uses
// (Controls::play_action) and SELECT picks up everything or, held, sets off
// the detpacs (Controls::select_button). Weapons and items are picked on the
// touch screen (Controls::touch).
const std::array<const PadAction*, kControlCount> kPlay = {
    action(kPadFire), action(kPadJump), action(kPadSelect), action(kPadUseItem),
    action(kPadCrouch), nullptr, action(kPadMenu), nullptr,
    &kEnergyCube, action(kPadMedkit), nullptr, nullptr,
};
// In menus and screens: the D-pad (or the circle pad) is the arrows, A is
// Enter, B and START are Escape (back, or "no"), X is "yes" and Y is Tab
// (Weasel's shop: weapons or ammunition).
const std::array<const PadAction*, kControlCount> kMenu = {
    action(kPadConfirm), action(kPadMenu), &kYes, action(kPadJump),
    nullptr, nullptr, action(kPadMenu), nullptr,
    action(kPadForward), action(kPadBackward), action(kPadTurnLeft), action(kPadTurnRight),
};
// The arrows the circle pad sends in menus, by 90 degree sector
// counter-clockwise from east.
const std::array<const PadAction*, 4> kMenuArrows = {
    action(kPadTurnRight), action(kPadForward), action(kPadTurnLeft), action(kPadBackward),
};

// Keypad keys (not extended) by 45 degree sector counter-clockwise from east,
// as the PC gamepad sends them: right, up-right (PgUp), up, up-left (Home),
// left, down-left (End), down, down-right (PgDn). With the game's
// directional_move, AvatarMover reads them as screen directions.
constexpr std::array<std::uint8_t, 8> kMoveKeys = {0x4D, 0x49, 0x48, 0x47, 0x4B, 0x4F, 0x50, 0x51};

class Controls {
public:
    explicit Controls(Game& game) : game_(game) {}

    void update() {
        const u32 held = hidKeysHeld();
        const bool menu = !game_.mouse_in_play();
        circlePosition pad;
        hidCircleRead(&pad);
        const std::array<bool, kControlCount> now = {
            (held & KEY_A) != 0, (held & KEY_B) != 0, (held & KEY_X) != 0, (held & KEY_Y) != 0,
            (held & KEY_L) != 0, (held & KEY_R) != 0, (held & KEY_START) != 0, (held & KEY_SELECT) != 0,
            (held & KEY_DUP) != 0, (held & KEY_DDOWN) != 0, (held & KEY_DLEFT) != 0, (held & KEY_DRIGHT) != 0,
        };
        // Last frame's step or roll comes up first.
        if (pulsed_) {
            key(pulsed_->scancode, pulsed_->extended, false);
            if (pulsed_->modifier) key(pulsed_->modifier, false, false);
            pulsed_ = nullptr;
        }
        const double t = now_us();
        for (int c = 0; c < kControlCount; ++c) {
            if (now[c] && !held_[c]) {
                press(c, menu ? kMenu[c] : play_action(c));
                if (!menu && (c == kPadLeft || c == kPadRight)) side_tap(c == kPadLeft ? 0 : 1, t);
            }
            if (!now[c] && held_[c]) release(c);
            held_[c] = now[c];
        }
        select_button(!menu && now[kSelect], t);
        if (menu) side_ = -1;
        else if (side_ >= 0 && t >= side_until_) {
            pulse(action(side_ == 0 ? kPadStepLeft : kPadStepRight));
            side_ = -1;
        }
        aim(!menu && now[kR], pad);
        if (menu) {
            move(-1);  // no move key held under a menu
            run(false);
            arrow(direction(pad, 4, arrow_dir_));
        } else if (aiming_) {
            arrow(-1);
            move(-1);  // the circle pad aims instead
            run(false);
        } else {
            arrow(-1);
            const int dir = direction(pad, 8, move_dir_);
            // Shift goes down before the move key, so the first step is already a run.
            run(dir >= 0 && far_out(pad, running_));
            move(dir);
        }
    }

    // A tap on the equipment (`shown`, as drawn): a weapon or item is made
    // active with the game's own keys, next weapon (W) or previous item (I)
    // once every other frame until it is (the game refuses a key while the
    // player is busy, so a few more are allowed).
    // Previous item, since going up the items never reaches the last slot.
    void touch(const InventoryPanel::State& shown) {
        if (tapped_) {
            key(tapped_->scancode, tapped_->extended, false);
            tapped_ = nullptr;
            return;
        }
        if (!shown.shown) {
            want_ = {};
            return;
        }
        if (hidKeysDown() & KEY_TOUCH) {
            touchPosition t;
            hidTouchRead(&t);
            const InventoryPanel::Hit hit = InventoryPanel::hit(t.px, t.py);
            switch (hit.target) {
            case InventoryPanel::Target::Weapon:
                if (shown.weapons[hit.slot - InventoryPanel::kFirstWeapon].id) want_ = hit;
                tries_ = kTries;
                break;
            case InventoryPanel::Target::Item:
                if (shown.items[hit.slot - InventoryPanel::kFirstItem].id) want_ = hit;
                tries_ = kTries;
                break;
            case InventoryPanel::Target::None: break;
            }
        }
        const bool weapon = want_.target == InventoryPanel::Target::Weapon;
        if (want_.target == InventoryPanel::Target::None) return;
        if ((weapon ? shown.active_weapon : shown.active_item) == want_.slot || tries_-- <= 0) {
            want_ = {};
            return;
        }
        tap(action(weapon ? kPadNextWeapon : kPadPrevItem));
    }

private:
    static constexpr int kTries = 30;
    // How long a D-pad left / right tap waits for a second one: two taps
    // roll, one steps aside once this has passed. A step cannot go at once
    // and become a roll on the second tap: the game drops a key pressed
    // while the player is busy (AvatarMover), so the roll would be lost.
    static constexpr double kDoubleTapUs = 180'000;

    // How long SELECT is held to set off the detpacs instead of picking up.
    static constexpr double kHoldUs = 400'000;

    // Y uses: the active item (U), or in the selection mode the selected
    // item, which Enter uses or picks up (U would leave an item on the floor).
    const PadAction* play_action(int control) const {
        if (control == kY && game_.selecting()) return action(kPadConfirm);
        return kPlay[control];
    }

    // SELECT, `down` at `t`: a short press picks up everything at its
    // release; held past kHoldUs it sets off the detpacs, once.
    void select_button(bool down, double t) {
        if (down && !select_down_) {
            select_since_ = t;
            select_held_ = false;
        } else if (down && !select_held_ && t - select_since_ >= kHoldUs) {
            pulse(&kDetonate);
            select_held_ = true;
        } else if (!down && select_down_ && !select_held_) {
            pulse(&kPickUpAll);
        }
        select_down_ = down;
    }

    // A D-pad left (0) or right (1) press at `t`.
    void side_tap(int side, double t) {
        if (side_ == side && t < side_until_) {
            pulse(action(side == 0 ? kPadRollLeft : kPadRollRight));
            side_ = -1;
            return;
        }
        // A tap of the other side first lets the pending one step at once.
        if (side_ >= 0) pulse(action(side_ == 0 ? kPadStepLeft : kPadStepRight));
        side_ = side;
        side_until_ = t + kDoubleTapUs;
    }

    // Pressed now (the modifier first), released at the next update(): the
    // modifier is held for one frame only, so that it does not reach the
    // repeats of a move key held meanwhile (Ctrl or Alt with a keypad key is
    // another move).
    void pulse(const PadAction* a) {
        if (pulsed_) {
            key(pulsed_->scancode, pulsed_->extended, false);
            if (pulsed_->modifier) key(pulsed_->modifier, false, false);
        }
        if (a->modifier) key(a->modifier, false, true);
        key(a->scancode, a->extended, true, a->ascii);
        pulsed_ = a;
    }

    // R held: the circle pad points the weapon (Game::set_aim_stick, which
    // draws it and enters the aiming mode) and the player stays put; with
    // the pad near the centre the aim keeps its angle, straight ahead at
    // first. Letting go ends the aiming mode at once.
    void aim(bool on, const circlePosition& pad) {
        if (on && !aiming_) aim_deg_ = game_.facing_screen_deg();
        if (!on) {
            if (aiming_) game_.leave_aim();
            aiming_ = false;
            return;
        }
        aiming_ = true;
        const double len = std::hypot(static_cast<double>(pad.dx), static_cast<double>(pad.dy));
        if (len > 40.0) {
            aim_deg_ = std::atan2(static_cast<double>(pad.dy), static_cast<double>(pad.dx)) * 180.0 / 3.14159265358979;
        }
        game_.set_aim_stick(true, aim_deg_);
    }

    // Pressed now, released at the next touch().
    void tap(const PadAction* a) {
        key(a->scancode, a->extended, true, a->ascii);
        tapped_ = a;
    }

    void key(std::uint8_t scancode, bool extended, bool pressed, std::uint8_t ascii = 0) {
        // The BIOS shift flags follow the modifier keys, as the keyboard ISR keeps them.
        std::uint16_t mod = 0;
        if (!extended) {
            if (scancode == kScCtrl) mod = kLeftCtrl | kCtrl;
            else if (scancode == kScAlt) mod = kLeftAlt | kAlt;
            else if (scancode == kScShift) mod = kLeftShift;
        }
        if (pressed) flags_ |= mod;
        else flags_ = static_cast<std::uint16_t>(flags_ & ~mod);
        game_.key(scancode, extended, pressed, flags_, pressed ? ascii : 0);
    }

    void press(int control, const PadAction* a) {
        if (!a || a->scancode == 0) return;
        sent_[control] = a;
        if (a->modifier) key(a->modifier, false, true);
        key(a->scancode, a->extended, true, a->ascii);
    }

    // The key sent at the press is the one released, even if a menu opened meanwhile.
    void release(int control) {
        const PadAction* a = sent_[control];
        if (!a) return;
        key(a->scancode, a->extended, false);
        if (a->modifier) key(a->modifier, false, false);
        sent_[control] = nullptr;
    }

    // The circle pad's screen direction, as one of `sectors` sectors
    // counter-clockwise from east, or -1 near the centre. The `held` sector is
    // kept until the pad leaves it by a third of a sector more, so that a push
    // between two does not flicker; the pad has to go further out to start
    // than to keep one.
    static int direction(const circlePosition& pad, int sectors, int held) {
        const double len = std::hypot(static_cast<double>(pad.dx), static_cast<double>(pad.dy));
        if (len <= (held >= 0 ? 40.0 : 60.0)) return -1;
        const double width = 360.0 / sectors;
        const double angle = std::atan2(static_cast<double>(pad.dy), static_cast<double>(pad.dx)) * 180.0 / 3.14159265358979;
        int want = ((static_cast<int>(std::lround(angle / width)) % sectors) + sectors) % sectors;
        if (held >= 0) {
            double off = std::fmod(std::fabs(angle - held * width), 360.0);
            if (off > 180.0) off = 360.0 - off;
            if (off < width * 2.0 / 3.0) want = held;
        }
        return want;
    }

    // Whether the circle pad is pushed far enough to run (it reads about 150
    // at the rim). As in direction(), a run is kept further in than it starts.
    static bool far_out(const circlePosition& pad, bool running) {
        const double len = std::hypot(static_cast<double>(pad.dx), static_cast<double>(pad.dy));
        return len > (running ? 105.0 : 120.0);
    }

    // Running is the original's Shift held while moving (AvatarMover); the
    // key repeat carries the current shift state, so a walk becomes a run (and
    // back) at the next step without releasing the move key.
    void run(bool on) {
        if (on == running_) return;
        key(kScShift, false, on);
        running_ = on;
    }

    // In menus the circle pad is one arrow key, pressed once per push.
    void arrow(int dir) {
        if (dir == arrow_dir_) return;
        const PadAction* old = arrow_dir_ >= 0 ? kMenuArrows[static_cast<std::size_t>(arrow_dir_)] : nullptr;
        if (old) key(old->scancode, old->extended, false);
        if (dir >= 0) {
            const PadAction* a = kMenuArrows[static_cast<std::size_t>(dir)];
            key(a->scancode, a->extended, true, a->ascii);
        }
        arrow_dir_ = dir;
    }

    // The new move key goes down before the old one comes up, so that the
    // player does not stop between two directions.
    void move(int dir) {
        if (dir == move_dir_) return;
        if (dir >= 0) key(kMoveKeys[dir], false, true);
        if (move_dir_ >= 0) key(kMoveKeys[move_dir_], false, false);
        move_dir_ = dir;
    }

    Game& game_;
    std::array<bool, kControlCount> held_{};
    std::array<const PadAction*, kControlCount> sent_{};
    std::uint16_t flags_ = 0;
    int move_dir_ = -1;
    int arrow_dir_ = -1;
    bool running_ = false;
    bool aiming_ = false;
    double aim_deg_ = 0;
    bool select_down_ = false;
    bool select_held_ = false;  // the hold has set off the detpacs
    double select_since_ = 0;
    int side_ = -1;  // a D-pad left (0) or right (1) tap waiting for a second
    double side_until_ = 0;
    const PadAction* pulsed_ = nullptr;
    const PadAction* tapped_ = nullptr;
    InventoryPanel::Hit want_;
    int tries_ = 0;
};

// The controls in menus, in the font of the movie subtitles (light, for a
// dark background).
// At most 14 lines: the 15th would reach the frame times at the bottom.
constexpr std::array<const char*, 13> kHelp = {
    "Crusader: No Remorse (3DS port)",
    "Circle pad  move, far: run",
    "A fire  B jump  X select  Y use",
    "L kneel / stand  R aim (hold)",
    "D-pad L / R  step, twice: roll",
    "D-pad up energy  down medkit",
    "SELECT  pick up all,",
    "  hold: detonate",
    "START  menu",
    "Touch  pick weapon / item",
    "Menus: D-pad or circle pad,",
    "  A enter, B back / no, X yes,",
    "  Y weapons / ammo (shop)",
};
static_assert(kHelp.size() <= 14);
constexpr int kHelpFont = 4;

// The bottom screen: the equipment in play, the controls otherwise, and the
// frame times. Single-buffered, written only when what it shows changes.
class BottomScreen {
public:
    explicit BottomScreen(Game& game)
        : game_(game), panel_(game.gumps(), game.fonts(), game.xform()),
          surface_(InventoryPanel::kWidth, InventoryPanel::kHeight) {}

    const InventoryPanel::State& state() const { return state_; }

    void update(const std::string& stats) {
        const InventoryPanel::State state =
            game_.mouse_in_play() ? InventoryPanel::read(game_.world(), game_.status_bar()) : InventoryPanel::State{};
        if (drawn_ && state == state_ && stats == stats_ && same_colours(game_.palette(), palette_)) return;
        state_ = state;
        stats_ = stats;
        palette_ = game_.palette();
        drawn_ = true;
        panel_.draw(surface_, game_.world(), state_, palette_);
        if (!state_.shown) {
            for (std::size_t i = 0; i < kHelp.size(); ++i) print(6, 6 + 15 * static_cast<int>(i), kHelp[i]);
        }
        print(4, InventoryPanel::kHeight - 12, stats_);
        ctr::draw_to_bottom_screen(surface_, palette_);
    }

private:
    // The glyphs' colours are palette entries, black under some palettes:
    // the text is drawn in the light grey nearest in the current palette.
    void print(int x, int y, const std::string& text) {
        // (x, y) is the pen, as TextWriter::print takes it; glyphs reach above
        // and below it.
        line_.clear(kNoInk);
        TextWriter(game_.fonts(), kHelpFont).print(line_, 0, kLinePen, text);
        const std::uint8_t ink = InventoryPanel::nearest(palette_, {216, 216, 216});
        for (int r = 0; r < line_.height; ++r) {
            const int to = y - kLinePen + r;
            if (to < 0 || to >= surface_.height) continue;
            const std::uint8_t* src = line_.row(r);
            std::uint8_t* dst = surface_.row(to);
            for (int c = 0; c < line_.width && x + c < surface_.width; ++c) {
                if (src[c] != kNoInk) dst[x + c] = ink;
            }
        }
    }

    static constexpr std::uint8_t kNoInk = 0xFF;
    static constexpr int kLinePen = 12;

    // The palette, cycled entries aside: their colours change every few frames
    // and the panel is not redrawn for them (they would pulse anyway).
    static bool same_colours(const Palette& a, const Palette& b) {
        for (int i = 0; i < 256; ++i) {
            if (i >= InventoryPanel::kFirstCycled && i <= InventoryPanel::kLastCycled) continue;
            const Rgb &x = a.colors[i], &y = b.colors[i];
            if (x.r != y.r || x.g != y.g || x.b != y.b) return false;
        }
        return true;
    }

    Game& game_;
    Surface8 line_{InventoryPanel::kWidth, 2 * kLinePen};
    InventoryPanel panel_;
    Surface8 surface_;
    InventoryPanel::State state_;
    Palette palette_;
    std::string stats_;
    bool drawn_ = false;
};

void wait_for_start() {
    std::printf("\nPress START to exit.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
}

}  // namespace

int main() {
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_BOTTOM, false);

    namespace fs = std::filesystem;
    const bool romfs = R_SUCCEEDED(romfsInit());
    std::error_code error;
    const fs::path data =
        romfs && fs::is_directory("romfs:/data", error) ? fs::path("romfs:/data") : fs::path(kRoot) / "data";
    fs::create_directories(fs::path(kRoot) / "SAVEGAME", error);

    try {
        // SHAPES.FLX (40 MiB) does not fit beside the rest: read per map.
        Game game(data, fs::path(kRoot) / "SAVEGAME", {}, true);
        game.set_config_file(fs::path(kRoot) / "CRUSADER.CFG");
        // The camera follows the player smoothly instead of jumping when he
        // nears the edge of the view, and the circle pad moves in screen
        // directions (keypad keys, AvatarMover's directional mode).
        game.config().follow_player = true;
        game.config().directional_move = true;
        game.config().draw_on_aim = true;  // R draws the weapon to aim
        game.start_up();
        game.set_cursor_drawn(false);  // no pointer: menus are played with keys
        game.set_status_bar_drawn(false);  // the bottom screen shows it
        game.set_menu_quit_shown(false);   // HOME closes the game
        Controls controls(game);
        BottomScreen bottom(game);
        std::string stats;
        Surface8 screen = Surface8::half(800, 480);

        double last = now_us(), fps_since = last;
        int frames = 0;
        double update_us = 0, render_us = 0, present_us = 0, slowest_us = 0;
        while (aptMainLoop()) {
            hidScanInput();
            const double t = now_us();
            // A long stall (loading, the system menu) is not played as game time.
            const double frame_us = std::min(t - last, 100'000.0);
            last = t;
            controls.update();
            controls.touch(bottom.state());
            game.update(static_cast<std::uint32_t>(frame_us));
            const double t_updated = now_us();
            if (game.quit_requested()) break;
            game.render(screen);
            const double t_rendered = now_us();
            ctr::draw_to_top_screen(screen, game.palette());
            bottom.update(stats);
            gfxFlushBuffers();
            gfxSwapBuffers();
            const double t_presented = now_us();
            gspWaitForVBlank();
            update_us += t_updated - t;
            render_us += t_rendered - t_updated;
            present_us += t_presented - t_rendered;
            slowest_us = std::max(slowest_us, frame_us);
            ++frames;
            // Once a second: the frame rate, the slowest frame and the average
            // time of each part (without the wait for the screen).
            if (t - fps_since >= 1'000'000.0) {
                char line[96];
                std::snprintf(line, sizeof line, "%4.1f fps max %3.0f u%4.1f d%4.1f s%4.1f",
                              frames * 1'000'000.0 / (t - fps_since), slowest_us / 1000.0,
                              update_us / frames / 1000.0, render_us / frames / 1000.0, present_us / frames / 1000.0);
                stats = line;
                frames = 0;
                fps_since = t;
                update_us = render_us = present_us = slowest_us = 0;
            }
        }
    } catch (const std::exception& e) {
        consoleInit(GFX_BOTTOM, nullptr);
        std::printf("\nerror: %s\n", e.what());
        wait_for_start();
    }

    if (romfs) romfsExit();
    gfxExit();
    return 0;
}
