#pragma once
// Isometric world rendering (DISPLAY.C / SPRITE.C / DLIST.C).
//
// Coordinates: world x/y = 2 * usecode x/y (what FIXED.DAT and getX store);
// z is unscaled. An item's location is the corner of its box nearest to the
// viewer at the bottom; the box extends towards -x, -y and +z by the shape's
// footpad (TYPEFLAG.DAT) * 32 for x/y and * 8 for z (checked against tile and
// wall frame sizes). Projection of a world point:
//   sx = x/4 - y/4        sy = x/8 + y/8 - z
// A frame is drawn with its anchor (xoff, yoff) on the projected location.
//
// Draw order: items whose screen rectangles overlap are ordered by the
// original's comparator (DLIST 11A0:0000): boxes apart on x, y, then z are
// drawn back to front; intersecting boxes by the depth of their far bottom
// corner (x + y + 2z). The constraints are then topologically sorted.

#include "core/flex.hpp"
#include "core/flex_file.hpp"
#include "core/shape.hpp"
#include "core/surface.hpp"
#include "engine/world/world.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace crusader {

constexpr int kFootpadXY = 32;  // world units per footpad unit (x, y)
constexpr int kFootpadZ = 8;    // world units per footpad unit (z)

// Item flags (Item::flags) used by rendering.
enum ItemFlag : std::uint16_t {
    kItemInvisible = 0x0010,
    kItemFlipped = 0x0020,  // drawn mirrored
};

struct Camera {
    std::int32_t x = 0;  // world units
    std::int32_t y = 0;
    std::int32_t z = 0;
};

inline std::int32_t world_from_usecode(std::int32_t v) { return v * 2; }

// Parses shapes of an archive on first use. Built on a FlexFile instead of a
// FlexArchive, it reads each shape from the file and holds its bytes, so the
// archive need not be in memory: load() reads many shapes in few requests and
// keep_only() frees the others. The pointers get() returns stay valid until
// keep_only() drops their shape.
class ShapeCache {
public:
    ShapeCache(const FlexArchive& archive, ShapeLayout layout) : archive_(&archive), layout_(layout) {}
    ShapeCache(FlexFile& file, ShapeLayout layout) : file_(&file), layout_(layout) {}
    const Shape* get(std::uint16_t shape);

    // From a FlexFile only (no-ops otherwise): reads the shapes not held yet,
    // and drops every shape not in `shapes`.
    void load(std::span<const std::uint16_t> shapes);
    void keep_only(std::span<const std::uint16_t> shapes);

    struct Stats {
        std::size_t shapes = 0;        // held now
        std::size_t bytes = 0;         // their file bytes, from a FlexFile
        std::size_t misses = 0;        // shapes get() read one by one from a FlexFile
        std::size_t requests = 0;      // file reads, misses included
    };
    Stats stats() const;

private:
    struct Entry {
        std::vector<std::uint8_t> bytes;  // from a FlexFile; the shape points into it
        std::optional<Shape> shape;
    };
    Entry& add(std::uint16_t shape, std::vector<std::uint8_t> bytes);

    const FlexArchive* archive_ = nullptr;
    FlexFile* file_ = nullptr;
    ShapeLayout layout_;
    std::unordered_map<std::uint16_t, Entry> cache_;
    std::size_t bytes_ = 0;
    std::size_t misses_ = 0;
};

struct DrawItem {
    const Item* item;
    const ShapeFrame* frame;
    int sx, sy;  // projected location on screen
    bool mirrored;
    // World box.
    std::int32_t x, y, z, x_left, y_far, z_top;
    // Screen rectangle.
    int left, top, right, bottom;
    int sx_left;                 // screen x of the box's left corner (x_left, y)
    std::uint32_t shape_flags;   // ShapeInfo::flags, for tie-breaking
};

struct RenderStats {
    std::size_t candidates = 0;   // items in view
    std::size_t drawn = 0;
    std::size_t missing_shape = 0;
    std::size_t cycle_edges = 0;  // ordering constraints dropped to break cycles
    // First dropped constraints: (item drawn first, item that should have been).
    std::vector<std::pair<const Item*, const Item*>> cycle_samples;
};

// Ordering constraint between two items: true when `a` must be drawn before
// `b`, false for the reverse, nullopt when they do not cover each other.
std::optional<bool> draw_before(const DrawItem& a, const DrawItem& b);

// Top-left corner of the view in projected coordinates (pixels).
struct View {
    int left = 0, top = 0;
};

// Items of `world` visible on a w x h screen whose top-left corner is `view`,
// in draw order.
std::vector<DrawItem> build_display_list(const World& world, ShapeCache& shapes, const View& view, int w, int h,
                                         RenderStats* stats = nullptr);
// Same, centred on `camera` (world units).
std::vector<DrawItem> build_display_list(const World& world, ShapeCache& shapes, const Camera& camera, int w,
                                         int h, RenderStats* stats = nullptr);

// Items of translucent shapes are blended through `xform` (opaque without it).
// `after` runs right after each item is drawn (overlays drawn with it).
void draw_display_list(Surface8& screen, const std::vector<DrawItem>& list, const XformTable* xform = nullptr,
                       const std::function<void(Surface8&, const DrawItem&)>& after = {});

// Indices (in draw order) of the items with an opaque pixel at screen (x, y).
std::vector<std::size_t> pick(const std::vector<DrawItem>& list, int x, int y);

}  // namespace crusader
