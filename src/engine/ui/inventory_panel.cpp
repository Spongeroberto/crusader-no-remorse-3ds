#include "engine/ui/inventory_panel.hpp"

#include "core/dtable.hpp"
#include "engine/world/actor_anim.hpp"
#include "engine/world/inventory.hpp"

#include "core/language.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
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
constexpr int kBoxHeight = 28;
constexpr int kBarX = 34, kBarY0 = 7, kBarY1 = 20;  // inside the box
constexpr std::uint8_t kHealthColour = 0x41;
constexpr std::uint8_t kEnergyColour = 0xF5;

// The pickup list's count font (pickup_area.hpp).
constexpr int kCountFont = 12;

// The look of the game's consoles: brushed gunmetal, the status bar boxes in a
// recess at the top, then two areas framed in steel (GUMPS.FLX shape 51, the
// thin frame of the load / save screens), weapons left and items right, each
// under its name in the menus' orange font (7). Inside an area the slots have
// no border of their own: the active one is outlined in green.
constexpr int kTop = 4;
constexpr int kHealthX = 4, kEnergyX = 114, kKeycardX = 238;
constexpr int kHeaderX = 3, kHeaderY = 3, kHeaderWidth = 314, kHeaderHeight = 30;
constexpr std::uint16_t kFrame = 51;
constexpr int kFrameBorder = 4;
constexpr int kLabelTop = 36;
constexpr int kLabelFont = 7;
constexpr int kAreaTop = 46;
constexpr int kGridTop = kAreaTop + kFrameBorder, kRowHeight = 32, kRowStep = 33;
constexpr int kAreaHeight = 2 * kFrameBorder + 4 * kRowStep + kRowHeight;
constexpr int kWeaponAreaX = 3, kWeaponX = kWeaponAreaX + kFrameBorder, kWeaponWidth = 106;
constexpr int kItemAreaX = kWeaponAreaX + 2 * kFrameBorder + kWeaponWidth + 3;
constexpr int kItemX = kItemAreaX + kFrameBorder, kItemColumns = 3, kCellWidth = 62, kColumnStep = 63;
constexpr int kItemAreaWidth = 2 * kFrameBorder + 2 * kColumnStep + kCellWidth;

// The metal, and the dark of the recess and the areas.
constexpr int kMetalGrey = 58, kMetalVariation = 22;
constexpr Rgb kInside = {14, 16, 20};
constexpr Rgb kRecess = {36, 38, 44};
constexpr Rgb kShadow = {20, 20, 22};
constexpr Rgb kHighlight = {120, 120, 126};
// The menus outline their focused line in green (colour 0x0C of MISC.PAL, as
// the options and controller screens; the difficulty screen's 0x44 is the
// same green in GAMEPAL.PAL): the active weapon and item are outlined so,
// with a darker green inside.
constexpr Rgb kFocus = {64, 216, 64};
constexpr Rgb kFocusDark = {30, 110, 30};

// A fixed noise in [-0.5, 0.5].
double noise(unsigned n) {
    n = n * 374761393u + 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return static_cast<double>((n ^ (n >> 16)) & 0xFFFF) / 65535.0 - 0.5;
}

bool inside(int x, int y, int x0, int y0, int w, int h) { return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h; }

// A rectangle sunk into the metal: dark above and left, light below and right.
void recess(Surface8& s, int x, int y, int w, int h, std::uint8_t inside, std::uint8_t shadow, std::uint8_t light) {
    fill_rect(s, x, y, x + w - 1, y + h - 1, inside);
    hline(s, x - 1, y - 1, x + w, shadow);
    vline(s, x - 1, y - 1, y + h, shadow);
    hline(s, x - 1, y + h, x + w, light);
    vline(s, x + w, y - 1, y + h, light);
}

// A rivet, its head lit from the top left.
void rivet(Surface8& s, int x, int y, std::uint8_t head, std::uint8_t edge, std::uint8_t shade) {
    const auto put = [&](int px, int py, std::uint8_t c) {
        if (std::uint8_t* p = s.at(px, py)) *p = c;
    };
    put(x, y, head);
    put(x + 1, y, edge);
    put(x, y + 1, edge);
    put(x + 1, y + 1, shade);
}

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

// The brushed metal under the current palette, worked out again only when the
// palette changes: lines of a grey varying a little, streaked along them.
const Surface8& InventoryPanel::metal(const Palette& palette) const {
    if (metal_.width == kWidth && same_palette(palette, metal_palette_)) return metal_;
    std::array<std::uint8_t, 256> grey{};
    for (int g = 0; g < 256; ++g) {
        const auto v = static_cast<std::uint8_t>(g);
        grey[g] = nearest(palette, {v, v, static_cast<std::uint8_t>(std::min(255, g + 4))});
    }
    metal_ = Surface8(kWidth, kHeight);
    for (int y = 0; y < kHeight; ++y) {
        const double line = noise(7 * y + 3) * kMetalVariation * 0.8;
        std::uint8_t* row = metal_.row(y);
        for (int x = 0; x < kWidth; ++x) {
            const double streak = noise(131 * y + 7 * (x / 23)) * kMetalVariation * 0.5 +
                                  noise(977 * y + x) * kMetalVariation * 0.6;
            row[x] = grey[std::clamp(static_cast<int>(std::lround(kMetalGrey + line + streak)), 0, 255)];
        }
    }
    metal_palette_ = palette;
    return metal_;
}

bool InventoryPanel::same_palette(const Palette& a, const Palette& b) {
    for (int i = 0; i < 256; ++i) {
        if (i >= kFirstCycled && i <= kLastCycled) continue;
        const Rgb &x = a.colors[i], &y = b.colors[i];
        if (x.r != y.r || x.g != y.g || x.b != y.b) return false;
    }
    return true;
}

// Gump `shape` frame 0 stretched over (x, y, w, h): its `border` pixels at
// each edge as they are, the rest repeated. The inside is left as it is.
void InventoryPanel::draw_frame(Surface8& screen, std::uint16_t shape, int x, int y, int w, int h,
                                int border) const {
    const Shape* s = gumps_->get(shape);
    if (!s || s->frames().empty()) return;
    const ShapeFrame& f = s->frames()[0];
    constexpr std::uint8_t kClear = 0xFF;
    Surface8 src(f.width, f.height, kClear);
    f.draw(src, f.xoff, f.yoff);
    const auto from = [&](int d, int len, int src_len) {
        if (d < border) return d;
        if (d >= len - border) return src_len - (len - d);
        return border + (d - border) % std::max(1, src_len - 2 * border);
    };
    for (int r = 0; r < h; ++r) {
        const std::uint8_t* src_row = src.row(from(r, h, f.height));
        const bool edge_row = r < border || r >= h - border;
        for (int c = 0; c < w; ++c) {
            if (!edge_row && c >= border && c < w - border) continue;
            const std::uint8_t v = src_row[from(c, w, f.width)];
            if (v == kClear) continue;
            if (std::uint8_t* p = screen.at(x + c, y + r)) *p = v;
        }
    }
}

void InventoryPanel::draw(Surface8& screen, const World& world, const State& state, const Palette& palette) const {
    if (!state.shown) {
        screen.clear(0);
        return;
    }
    const Surface8& plate = metal(palette);
    for (int y = 0; y < kHeight && y < screen.height; ++y) {
        std::copy_n(plate.row(y), std::min(kWidth, screen.width), screen.row(y));
    }
    const std::uint8_t inside_colour = nearest(palette, kInside);
    const std::uint8_t shadow = nearest(palette, kShadow);
    const std::uint8_t light = nearest(palette, kHighlight);
    const std::uint8_t focus = nearest(palette, kFocus);
    const std::uint8_t focus_dark = nearest(palette, kFocusDark);
    const auto outline = [&](int x, int y, int w, int h) {
        for (int k = 0; k < 2; ++k) {
            const std::uint8_t c = k == 0 ? focus : focus_dark;
            hline(screen, x + k, y + k, x + w - 1 - k, c);
            hline(screen, x + k, y + h - 1 - k, x + w - 1 - k, c);
            vline(screen, x + k, y + k, y + h - 1 - k, c);
            vline(screen, x + w - 1 - k, y + k, y + h - 1 - k, c);
        }
    };
    TextWriter count(*fonts_, kCountFont);

    // Health and energy, as the status bar draws them, in a recess.
    recess(screen, kHeaderX, kHeaderY, kHeaderWidth, kHeaderHeight, nearest(palette, kRecess), shadow, light);
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

    // The two areas, under their names.
    TextWriter label(*fonts_, kLabelFont);
    label.print(screen, kWeaponAreaX + 2, kLabelTop + label.baseline(), tr("WEAPONS"));
    label.print(screen, kItemAreaX + 2, kLabelTop + label.baseline(), tr("ITEMS"));
    const int weapon_area_width = 2 * kFrameBorder + kWeaponWidth;
    fill_rect(screen, kWeaponAreaX, kAreaTop, kWeaponAreaX + weapon_area_width - 1, kAreaTop + kAreaHeight - 1,
              inside_colour);
    fill_rect(screen, kItemAreaX, kAreaTop, kItemAreaX + kItemAreaWidth - 1, kAreaTop + kAreaHeight - 1,
              inside_colour);
    draw_frame(screen, kFrame, kWeaponAreaX, kAreaTop, weapon_area_width, kAreaHeight, kFrameBorder);
    draw_frame(screen, kFrame, kItemAreaX, kAreaTop, kItemAreaWidth, kAreaHeight, kFrameBorder);
    const std::uint8_t head = nearest(palette, {200, 200, 205}), edge = nearest(palette, {130, 130, 136});
    for (const int x : {1, kWidth - 3}) {
        for (const int y : {kAreaTop - 10, kHeight - 3}) rivet(screen, x, y, head, edge, shadow);
    }

    for (int i = 0; i < kWeaponSlots; ++i) {
        const Slot& w = state.weapons[i];
        if (!w.id) continue;
        const int y = kGridTop + kRowStep * i;
        if (kFirstWeapon + i == state.active_weapon) outline(kWeaponX, y, kWeaponWidth, kRowHeight);
        draw_gump(screen, 3, dtable::weapon_icon(w.shape), kWeaponX + 44, y + kRowHeight / 2, false);
        // As the status bar's ammunition box: rounds in the weapon, clips carried.
        if (w.clips >= 0) {
            count.print(screen, kWeaponX + 82, y + 8, std::to_string(w.quality));
            count.print(screen, kWeaponX + 82, y + kRowHeight - 10, std::to_string(w.clips));
        }
    }

    for (int i = 0; i < kItemSlots; ++i) {
        const Slot& s = state.items[i];
        const Item* it = s.id ? world.get(s.id) : nullptr;
        if (!it) continue;
        const int x = item_x(i), y = item_y(i);
        if (kFirstItem + i == state.active_item) outline(x, y, kCellWidth, kRowHeight);
        const StatusBar::Icon icon = StatusBar::item_icon(world, *it);
        draw_gump(screen, icon.shape, icon.frame, x + 27, y + kRowHeight / 2, false);
        if (s.quality > 1) count.print(screen, x + 50, y + kRowHeight - 8, std::to_string(s.quality));
    }
}

InventoryPanel::Hit InventoryPanel::hit(int x, int y) {
    for (int i = 0; i < kWeaponSlots; ++i) {
        if (inside(x, y, kWeaponX, kGridTop + kRowStep * i, kWeaponWidth, kRowHeight)) {
            return {Target::Weapon, kFirstWeapon + i};
        }
    }
    for (int i = 0; i < kItemSlots; ++i) {
        if (inside(x, y, item_x(i), item_y(i), kCellWidth, kRowHeight)) return {Target::Item, kFirstItem + i};
    }
    return {};
}

}  // namespace crusader
