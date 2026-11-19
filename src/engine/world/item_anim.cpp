#include "engine/world/item_anim.hpp"

#include "engine/usecode/world_intrinsics.hpp"

#include <algorithm>
#include <vector>

namespace crusader {

namespace {

// random_range (12D8:04FF): a draw in 0..n-1, 0 for n = 0.
int draw(World& world, int n) { return random_range(world.state.random_state, static_cast<std::uint16_t>(n)); }

// Types 1, 3 and 6: a step within the block of `d` frames holding `f`.
int block_step(int f, int d) { return (f + 1) % d + f - f % d; }

}  // namespace

// @orig 1198:162B 1198:1398
void animate_items(World& world, std::uint32_t count, std::uint32_t last, const FrameCount& frames) {
    std::vector<ObjId> due;
    const auto consider = [&](const Item& item) {
        if (item.parent != 0 || item.sprite_only || (item.flags & (kItemEthereal | kItemContained))) return;
        if (!(item.flags & kItemFastArea)) return;
        const ShapeInfo* info = world.shape_info(item.shape);
        if (!info || info->anim_type == 0) return;
        const std::uint32_t speed = info->anim_speed;
        if (speed >= 2 && last / speed == count / speed) return;
        if (!world.is_partly_on_screen(item)) return;
        due.push_back(item.id);
    };
    // Only items partly on screen animate. Port: with a reach bound, they are
    // looked for in the cells near the view and the NPC slots (the only items
    // off the map), then taken in id order, as walking every item gives,
    // since the order decides who draws which random number. Without one,
    // or with a view where the original's 16-bit screen arithmetic could
    // wrap around (World::is_partly_in), every item is visited.
    const GameState& s = world.state;
    const ShapeReach* reach = world.reach();
    const bool view_in_range = s.view_left >= -0x4000 && s.view_left <= 0x4000 && s.view_top >= -0x4000 &&
                               s.view_top <= 0x7EFF;
    if (reach && world.items_in_grid() && view_in_range) {
        std::vector<ObjId> near;
        world.for_each_near_view(s.view_left, s.view_top, s.screen_right + 1, s.screen_bottom + 1, *reach,
                                 [&](const Item& item) { near.push_back(item.id); });
        for (ObjId id = 1; id <= 0xFF; ++id) near.push_back(id);
        std::sort(near.begin(), near.end());
        near.erase(std::unique(near.begin(), near.end()), near.end());
        for (ObjId id : near) {
            if (const Item* item = world.get(id)) consider(*item);
        }
    } else {
        world.for_each(consider);
    }
    for (ObjId id : due) {
        Item* item = world.get(id);
        if (!item) continue;
        const ShapeInfo* info = world.shape_info(item->shape);
        const int n = frames(item->shape);
        const int d = info->anim_data;
        const int f = item->frame;
        int next = -1;
        switch (info->anim_type) {
            case 1:
            case 3:
                if (d == 0 || (d == 1 && draw(world, 2) != 0)) {
                    next = f + 1 >= n ? 0 : f + 1;
                } else {
                    next = block_step(f, d);
                    if (next >= n) next -= next % d;
                }
                break;
            case 2:
                if (draw(world, 2) != 0) next = draw(world, n);
                break;
            case 4:
                if (f != 0 || draw(world, d + 2) == 0) next = f + 1 >= n ? 0 : f + 1;
                break;
            case 6:
                if (d == 0 || (d == 1 && draw(world, 2) != 0)) {
                    if (f != 0) next = f + 1 >= n ? 1 : f + 1;
                } else if (f % d != 0) {
                    next = block_step(f, d);
                    if (next % d == 0) ++next;
                    if (next >= n) next -= next % d;
                }
                break;
            default:  // 5: ITEM_10a8_28E2, unused by the data
                break;
        }
        if (next >= 0) world.set_frame(*item, static_cast<std::uint16_t>(next));
    }
}

}  // namespace crusader
