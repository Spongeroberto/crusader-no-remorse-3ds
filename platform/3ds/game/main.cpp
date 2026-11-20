// Crusader: No Remorse on a 3DS: the game's 800 x 480 view drawn at half
// resolution on the top screen, the controls on the bottom one. Buttons send
// the original's keys, as the PC version's gamepad does (core/pad_config.hpp).
// There is no mouse: the cursor is never drawn and every menu, screen and
// question is answered with keys. Data comes from romfs:/data when the
// CIA bundles it, else from sdmc:/3ds/crusader/data; saves and settings go to
// sdmc:/3ds/crusader. No sound yet.

#include "core/pad_config.hpp"
#include "engine/input/keyboard.hpp"
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

// In play. The circle pad moves (see Controls::move).
const std::array<const PadAction*, kControlCount> kPlay = {
    action(kPadFire), action(kPadJump), action(kPadSelect), action(kPadNextWeapon),
    action(kPadCrouch), action(kPadShift), action(kPadMenu), action(kPadConfirm),
    action(kPadUseItem), action(kPadMedkit), action(kPadPrevItem), action(kPadNextItem),
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
        for (int c = 0; c < kControlCount; ++c) {
            if (now[c] && !held_[c]) press(c, menu ? kMenu[c] : kPlay[c]);
            if (!now[c] && held_[c]) release(c);
            held_[c] = now[c];
        }
        if (menu) {
            move(-1);  // no move key held under a menu
            arrow(direction(pad, 4, arrow_dir_));
        } else {
            arrow(-1);
            move(direction(pad, 8, move_dir_));
        }
    }

private:
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
};

void print_help() {
    std::printf("\x1b[2J\x1b[1;1H");
    std::printf("Crusader: No Remorse (3DS port)\n\n");
    std::printf(" Circle pad  move (screen direction)\n");
    std::printf(" A  fire        B  jump\n");
    std::printf(" X  select      Y  next weapon\n");
    std::printf(" L  kneel       R  run (hold)\n");
    std::printf(" D-pad  use item, medkit,\n");
    std::printf("        previous / next item\n");
    std::printf(" SELECT  use selected (Enter)\n");
    std::printf(" START   menu\n");
    std::printf(" Menus: D-pad or circle pad, A enter,\n");
    std::printf("        B back / no, X yes,\n");
    std::printf("        Y weapons / ammo (shop)\n");
    std::printf(" START + SELECT  quit\n\n");
}

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
    consoleInit(GFX_BOTTOM, nullptr);
    print_help();

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
        game.start_up();
        game.set_cursor_drawn(false);  // no pointer: menus are played with keys
        Controls controls(game);
        Surface8 screen = Surface8::half(800, 480);

        double last = now_us(), fps_since = last;
        int frames = 0;
        double update_us = 0, render_us = 0, present_us = 0, slowest_us = 0;
        while (aptMainLoop()) {
            hidScanInput();
            if ((hidKeysHeld() & (KEY_START | KEY_SELECT)) == (KEY_START | KEY_SELECT)) break;
            const double t = now_us();
            // A long stall (loading, the system menu) is not played as game time.
            const double frame_us = std::min(t - last, 100'000.0);
            last = t;
            controls.update();
            game.update(static_cast<std::uint32_t>(frame_us));
            const double t_updated = now_us();
            if (game.quit_requested()) break;
            game.render(screen);
            const double t_rendered = now_us();
            ctr::draw_to_top_screen(screen, game.palette());
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
                std::printf("\x1b[22;1H %4.1f fps, slowest %3.0f ms      \n", frames * 1'000'000.0 / (t - fps_since),
                            slowest_us / 1000.0);
                std::printf(" update %4.1f render %4.1f show %4.1f ms  \n", update_us / frames / 1000.0,
                            render_us / frames / 1000.0, present_us / frames / 1000.0);
                // Where things stand: in a menu or in play, the map, the player.
                const Item* player = game.world().get(1);
                std::printf(" %s map %3d          \n", game.mouse_in_play() ? "play" : "menu",
                            game.world().current_map());
                std::printf(" player %5d,%5d,%3d          \n", player ? player->x : 0, player ? player->y : 0,
                            player ? player->z : 0);
                frames = 0;
                fps_since = t;
                update_us = render_us = present_us = slowest_us = 0;
            }
        }
    } catch (const std::exception& e) {
        std::printf("\nerror: %s\n", e.what());
        wait_for_start();
    }

    if (romfs) romfsExit();
    gfxExit();
    return 0;
}
