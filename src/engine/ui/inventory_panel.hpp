#pragma once
// The player's equipment on a second screen of 320 x 240 (the 3DS bottom
// screen). A port addition: the original has a single screen. It is drawn
// with the game's own gumps and fonts, on brushed gunmetal like its consoles:
//
//   y   3  a recess holding the health box and energy box (status bar boxes 3
//          and 4) and the keycards
//   y  36  "WEAPONS" and "ITEMS" in the menus' orange font (7)
//   y  46  two areas framed in steel (GUMPS.FLX shape 51): five weapons left
//          (the weapon icon, the rounds in it over the clips carried), fifteen
//          items right, three by five (the item's icon and count). Rows are
//          33 apart; the slots have no border of their own.
//
// The view leaves the status bar out (Game::set_status_bar_drawn), and the
// panel's colours are never palette entries the game cycles, so nothing on it
// pulses.
//
// Each equipment slot keeps its place, so an item does not move when another
// is used up. The active weapon and item are outlined in the menus' focus
// green. The health and energy boxes are translucent, as over the view, on a
// dark grey (over black the game's translucency leaves black); drawn solid
// they would show colours the palette cycles.

#include "core/font.hpp"
#include "core/palette.hpp"
#include "core/surface.hpp"
#include "core/xform.hpp"
#include "engine/ui/status_bar.hpp"
#include "engine/world/render.hpp"
#include "engine/world/world.hpp"

#include <array>
#include <cstdint>

namespace crusader {

class InventoryPanel {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 240;
    static constexpr int kWeaponSlots = 5;  // equipment slots 1..5
    static constexpr int kFirstWeapon = 1;
    static constexpr int kItemSlots = 15;  // equipment slots 11..25
    static constexpr int kFirstItem = 11;

    struct Slot {
        ObjId id = 0;
        std::uint16_t shape = 0;
        std::uint16_t frame = 0;
        std::int16_t quality = 0;
        std::int16_t clips = -1;  // weapons: clips carried, -1 without ammunition
        bool operator==(const Slot&) const = default;
    };

    // What the panel shows; it needs drawing again only when this changes.
    struct State {
        bool shown = false;  // the player is the controlled NPC
        std::array<Slot, kWeaponSlots> weapons{};
        std::array<Slot, kItemSlots> items{};
        int active_weapon = 0;  // equipment slot, 0 for none
        int active_item = 0;
        int health_width = -1;  // as StatusBar::Contents
        int energy_width = -1;
        int keycards = 0;
        bool operator==(const State&) const = default;
    };

    enum class Target { None, Weapon, Item };
    struct Hit {
        Target target = Target::None;
        int slot = 0;  // equipment slot of a weapon or item
    };

    InventoryPanel(ShapeCache& gumps, const FontSet& fonts, const XformTable& xform);

    static State read(const World& world, const StatusBar& bar);
    void draw(Surface8& screen, const World& world, const State& state, const Palette& palette) const;
    // Entries the game's palette cycling changes (palette_cycle.hpp).
    static constexpr int kFirstCycled = 1;
    static constexpr int kLastCycled = 14;
    // The palette entry closest to `want`, never a cycled one.
    static std::uint8_t nearest(const Palette& palette, Rgb want);
    // What is under (x, y), whether the slot holds something or not.
    static Hit hit(int x, int y);

private:
    void draw_gump(Surface8& screen, std::uint16_t shape, std::uint16_t frame, int x, int y, bool translucent) const;
    void draw_frame(Surface8& screen, std::uint16_t shape, int x, int y, int w, int h, int border) const;
    const Surface8& metal(const Palette& palette) const;
    static bool same_palette(const Palette& a, const Palette& b);

    ShapeCache* gumps_;
    const FontSet* fonts_;
    const XformTable* xform_;
    mutable Surface8 metal_;  // the background, for metal_palette_
    mutable Palette metal_palette_;
};

}  // namespace crusader
