#include "engine/ui/inventory_panel.hpp"

#include "core/dtable.hpp"
#include "engine/world/actor_anim.hpp"
#include "engine/world/inventory.hpp"

#include <bit>
#include <string>

namespace crusader {

namespace {

constexpr ObjId kPlayer = 1;
// The keycard's pickup list icon (pickup_list.hpp, a keycard of frame 0).
constexpr std::uint16_t kKeycardIcon = 5;
constexpr std::uint16_t kKeycardFrame = 13;

// Status bar gumps (status_bar.hpp): the box shape and its frames.
constexpr std::uint16_t kBox = 1;
constexpr std::uint16_t kWeaponBox = 0;
constexpr std::uint16_t kHealthBox = 2;
constexpr std::uint16_t kEnergyBox = 3;
constexpr int kBoxWidth = 108;
constexpr int kBoxHeight = 28;
constexpr int kBarX = 34, kBarY0 = 7, kBarY1 = 20;  // inside the box
constexpr std::uint8_t kHealthColour = 0x41;
constexpr std::uint8_t kEnergyColour = 0xF5;

// Pickup list gumps and fonts (pickup_area.hpp).
constexpr std::uint16_t kTile = 2;
constexpr int kTileSize = 28;
constexpr int kNameFont = 13;
constexpr int kCountFont = 12;

constexpr int kTop = 4;
constexpr int kHealthX = 4, kEnergyX = 114, kKeycardX = 238;
// The weapon boxes and item cells are taller than their gumps, to fill the
// screen: see draw_tall.
constexpr int kGridTop = 36, kRowHeight = 33, kRowStep = 37;
constexpr int kWeaponX = 4;
constexpr int kItemX = 122, kItemColumns = 3, kCellWidth = 2 * kTileSize, kColumnStep = 66;

constexpr Rgb kBackground = {40, 44, 52};
// The menus outline their focused line in green (colour 0x0C of MISC.PAL, as
// the options and controller screens; the difficulty screen's 0x44 is the
// same green in GAMEPAL.PAL): the active weapon and item are outlined so.
constexpr Rgb kFocus = {64, 216, 64};

bool inside(int x, int y, int x0, int y0, int w, int h) { return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h; }

int item_x(int i) { return kItemX + kColumnStep * (i % kItemColumns); }
int item_y(int i) { return kGridTop + kRowStep * (i / kItemColumns); }

InventoryPanel::Slot read_slot(const World& world, int equipment_slot) {
    InventoryPanel::Slot s;
    s.id = world.npc_equip(kPlayer, equipment_slot);
    const Item* it = s.id ? world.get(s.id) : nullptr;
    if (!it) return {};
    s.shape = it->shape;
    s.frame = it->frame;
    s.quality = static_cast<std::int16_t>(it->quality);
    return s;
}

}  // namespace

std::uint8_t InventoryPanel::nearest(const Palette& palette, Rgb want) {
    int best = 0, best_d = 1 << 30;
    for (int i = 0; i < 256; ++i) {
        if (i >= kFirstCycled && i <= kLastCycled) continue;
        const Rgb& c = palette.colors[i];
        const int dr = c.r - want.r, dg = c.g - want.g, db = c.b - want.b;
        const int d = dr * dr + dg * dg + db * db;
        if (d < best_d) {
            best = i;
            best_d = d;
        }
    }
    return static_cast<std::uint8_t>(best);
}

InventoryPanel::InventoryPanel(ShapeCache& gumps, const FontSet& fonts, const XformTable& xform)
    : gumps_(&gumps), fonts_(&fonts), xform_(&xform) {}

InventoryPanel::State InventoryPanel::read(const World& world, const StatusBar& bar) {
    State s;
    s.shown = world.state.controlled_npc == kPlayer && world.get(kPlayer) != nullptr;
    if (!s.shown) return s;
    for (int i = 0; i < kWeaponSlots; ++i) {
        Slot& w = s.weapons[i] = read_slot(world, kFirstWeapon + i);
        if (!w.id) continue;
        if (const std::uint16_t ammo = dtable::ammo_shape(w.shape)) {
            const Item* clips = world.get(carried(world, ammo));
            w.clips = clips ? static_cast<std::int16_t>(clips->quality) : 0;
        }
    }
    for (int i = 0; i < kItemSlots; ++i) s.items[i] = read_slot(world, kFirstItem + i);
    s.active_weapon = world.npc_word(kPlayer, npc::kWeapon);
    s.active_item = world.npc_word(kPlayer, npc::kActiveItem);
    const StatusBar::Contents c = bar.contents(world);
    s.health_width = c.health_width;
    s.energy_width = c.energy_width;
    s.keycards = std::popcount(world.state.keycards);
    return s;
}

void InventoryPanel::draw_gump(Surface8& screen, std::uint16_t shape, std::uint16_t frame, int x, int y,
                               bool translucent) const {
    const Shape* s = gumps_->get(shape);
    if (!s || frame >= s->frames().size()) return;
    s->frames()[frame].draw(screen, x, y, false, translucent ? xform_ : nullptr);
}

// The gump `h` pixels high: its top and bottom halves as they are, the row
// between them repeated, so that the edges keep their look.
void InventoryPanel::draw_tall(Surface8& screen, std::uint16_t shape, std::uint16_t frame, int x, int y, int h,
                               bool translucent) const {
    const Shape* s = gumps_->get(shape);
    if (!s || frame >= s->frames().size()) return;
    const ShapeFrame& f = s->frames()[frame];
    const XformTable* xform = translucent ? xform_ : nullptr;
    if (h <= f.height) {
        f.draw(screen, x, y, false, xform);
        return;
    }
    const int top = f.height / 2, bottom = f.height - top;
    const int left = x - f.xoff, w = f.width;
    Surface8 head = Surface8::window(screen, left, y - f.yoff, w, top);
    f.draw(head, f.xoff, f.yoff, false, xform);
    for (int r = top; r < h - bottom; ++r) {
        Surface8 row = Surface8::window(screen, left, y - f.yoff + r, w, 1);
        f.draw(row, f.xoff, f.yoff - top, false, xform);
    }
    Surface8 foot = Surface8::window(screen, left, y - f.yoff + h - bottom, w, bottom);
    f.draw(foot, f.xoff, f.yoff - top, false, xform);
}

void InventoryPanel::draw(Surface8& screen, const World& world, const State& state, const Palette& palette) const {
    if (!state.shown) {
        screen.clear(0);
        return;
    }
    screen.clear(nearest(palette, kBackground));
    const std::uint8_t focus = nearest(palette, kFocus);
    const auto outline = [&](int x, int y, int w, int h) {
        hline(screen, x - 1, y - 1, x + w, focus);
        hline(screen, x - 1, y + h, x + w, focus);
        vline(screen, x - 1, y, y + h - 1, focus);
        vline(screen, x + w, y, y + h - 1, focus);
    };
    TextWriter count(*fonts_, kCountFont);
    TextWriter name(*fonts_, kNameFont);

    // Health and energy, as the status bar draws them.
    draw_gump(screen, kBox, kHealthBox, kHealthX, kTop, true);
    if (state.health_width >= 0) {
        fill_rect(screen, kHealthX + kBarX, kTop + kBarY0, kHealthX + kBarX + state.health_width, kTop + kBarY1,
                  kHealthColour);
    }
    draw_gump(screen, kBox, kEnergyBox, kEnergyX, kTop, true);
    if (state.energy_width >= 0) {
        fill_rect(screen, kEnergyX + kBarX, kTop + kBarY0, kEnergyX + kBarX + state.energy_width, kTop + kBarY1,
                  kEnergyColour);
    }
    if (state.keycards > 0) {
        draw_gump(screen, kKeycardIcon, kKeycardFrame, kKeycardX, kTop + kBoxHeight / 2, false);
        count.print(screen, kKeycardX + 12, kTop + 20, std::to_string(state.keycards));
    }

    for (int i = 0; i < kWeaponSlots; ++i) {
        const Slot& w = state.weapons[i];
        const int y = kGridTop + kRowStep * i;
        draw_tall(screen, kBox, kWeaponBox, kWeaponX, y, kRowHeight, true);
        if (!w.id) continue;
        if (kFirstWeapon + i == state.active_weapon) outline(kWeaponX, y, kBoxWidth, kRowHeight);
        draw_gump(screen, 3, dtable::weapon_icon(w.shape), kWeaponX + 46, y + kRowHeight / 2, false);
        // As the status bar's ammunition box: rounds in the weapon, clips carried.
        if (w.clips >= 0) {
            count.print(screen, kWeaponX + 84, y + 8, std::to_string(w.quality));
            count.print(screen, kWeaponX + 84, y + kRowHeight - 10, std::to_string(w.clips));
        }
    }

    for (int i = 0; i < kItemSlots; ++i) {
        const Slot& s = state.items[i];
        const int x = item_x(i), y = item_y(i);
        draw_tall(screen, kTile, 0, x, y, kRowHeight, true);
        draw_tall(screen, kTile, 0, x + kTileSize, y, kRowHeight, true);
        const Item* it = s.id ? world.get(s.id) : nullptr;
        if (!it) continue;
        if (kFirstItem + i == state.active_item) outline(x, y, kCellWidth, kRowHeight);
        const StatusBar::Icon icon = StatusBar::item_icon(world, *it);
        draw_gump(screen, icon.shape, icon.frame, x + 24, y + kRowHeight / 2, false);
        if (s.quality > 1) count.print(screen, x + 44, y + kRowHeight - 8, std::to_string(s.quality));
    }
}

InventoryPanel::Hit InventoryPanel::hit(int x, int y) {
    for (int i = 0; i < kWeaponSlots; ++i) {
        if (inside(x, y, kWeaponX, kGridTop + kRowStep * i, kBoxWidth, kRowHeight)) {
            return {Target::Weapon, kFirstWeapon + i};
        }
    }
    for (int i = 0; i < kItemSlots; ++i) {
        if (inside(x, y, item_x(i), item_y(i), kCellWidth, kRowHeight)) return {Target::Item, kFirstItem + i};
    }
    return {};
}

}  // namespace crusader
