// Checks ShapeFrame::draw against a plain reference blitter, pixel for pixel,
// on every frame of every shape in the original data (data/, not in the
// repository): runs locally only. Each frame is drawn at placements that clip
// it on every edge and corner, by one pixel and by half, fully off screen and
// larger than the screen, mirrored and not, opaque and translucent, over a
// background that varies so translucent blends read different pixels.
// Usage: shape_draw_tests <data_dir>   (skips with exit code 0 when data is absent)

#include "core/bytes.hpp"
#include "core/flex.hpp"
#include "core/shape.hpp"
#include "core/surface.hpp"
#include "core/xform.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

using namespace crusader;

namespace {

int failures = 0;

// The blitter as it was before it was optimised: one pixel at a time, every
// pixel bounds-checked. It is the specification the fast one must match.
void reference_draw(const ShapeFrame& f, Surface8& dst, int x, int y, bool mirrored, const XformTable* xform) {
    const int top = y - f.yoff;
    const int left = mirrored ? x + f.xoff - (f.width - 1) : x - f.xoff;
    for (int ly = 0; ly < f.height; ++ly) {
        const int sy = top + ly;
        std::size_t p = f.lines[ly];
        int lx = 0;
        while (lx < f.width) {
            lx += read_u8(f.rle, p++);
            if (lx >= f.width) break;
            int len = read_u8(f.rle, p++);
            bool fill = false;
            if (f.compressed) {
                fill = len & 1;
                len >>= 1;
            }
            const bool visible_row = sy >= 0 && sy < dst.height;
            const int drawn = std::min(len, f.width - lx);
            for (int k = 0; k < drawn; ++k) {
                const std::uint8_t c = fill ? read_u8(f.rle, p) : read_u8(f.rle, p + k);
                const int fx = lx + k;
                const int sx = mirrored ? left + (f.width - 1 - fx) : left + fx;
                if (visible_row && sx >= 0 && sx < dst.width) {
                    std::uint8_t& out = dst.row(sy)[sx];
                    out = xform ? xform->blend(c, out) : c;
                }
            }
            p += fill ? 1 : len;
            lx += len;
        }
    }
}

void fill_background(Surface8& s) {
    for (int y = 0; y < s.height; ++y) {
        for (int x = 0; x < s.width; ++x) s.row(y)[x] = static_cast<std::uint8_t>(x * 7 + y * 13 + 1);
    }
}

struct Placement {
    int left, top;  // where the frame's top-left corner lands on the surface
};

// Frame corners placed so each surface edge cuts the frame after one pixel,
// through the middle, flush, and not at all.
std::vector<Placement> placements(int w, int h, int sw, int sh) {
    const int lefts[] = {4, -(w - 1), -w / 2, -w, sw - 1, sw - w / 2, sw - w, sw};
    const int tops[] = {4, -(h - 1), -h / 2, -h, sh - 1, sh - h / 2, sh - h, sh};
    std::vector<Placement> out;
    for (int l : lefts) out.push_back({l, 4});
    for (int t : tops) out.push_back({4, t});
    for (int l : {-w / 2, sw - w / 2}) {
        for (int t : {-h / 2, sh - h / 2}) out.push_back({l, t});
    }
    return out;
}

struct Totals {
    std::size_t frames = 0, draws = 0, mismatches = 0;
    double reference_s = 0, fast_s = 0;
};

using Clock = std::chrono::steady_clock;

void compare(const ShapeFrame& f, Surface8& a, Surface8& b, Placement pl, bool mirrored, const XformTable* xform,
             const std::string& where, Totals& t) {
    // Anchor that puts the frame's top-left corner at pl, mirrored or not.
    const int x = mirrored ? pl.left - f.xoff + f.width - 1 : pl.left + f.xoff;
    const int y = pl.top + f.yoff;
    fill_background(a);
    std::memcpy(b.pixels.data(), a.pixels.data(), a.pixels.size());
    const auto t0 = Clock::now();
    reference_draw(f, a, x, y, mirrored, xform);
    const auto t1 = Clock::now();
    f.draw(b, x, y, mirrored, xform);
    const auto t2 = Clock::now();
    t.reference_s += std::chrono::duration<double>(t1 - t0).count();
    t.fast_s += std::chrono::duration<double>(t2 - t1).count();
    ++t.draws;
    if (a.pixels == b.pixels) return;
    if (t.mismatches++ < 10) {
        std::size_t i = 0;
        while (a.pixels[i] == b.pixels[i]) ++i;
        std::fprintf(stderr, "  %s: %dx%d at (%d,%d) on %dx%d%s%s: first difference at (%zu,%zu)\n", where.c_str(),
                     f.width, f.height, pl.left, pl.top, a.width, a.height, mirrored ? " mirrored" : "",
                     xform ? " translucent" : "", i % a.width, i / a.width);
    }
}

// At half resolution (Surface8::half): draw() must give the full-size
// reference picture with every other pixel of every other line kept.
void compare_half(const ShapeFrame& f, Surface8& full, Surface8& half, Placement pl, bool mirrored,
                  const XformTable* xform, const std::string& where, Totals& t) {
    const int x = mirrored ? pl.left - f.xoff + f.width - 1 : pl.left + f.xoff;
    const int y = pl.top + f.yoff;
    fill_background(full);
    keep_every_other_pixel(full, half);
    reference_draw(f, full, x, y, mirrored, xform);
    f.draw(half, x, y, mirrored, xform);
    Surface8 expected = Surface8::half(full.width, full.height);
    keep_every_other_pixel(full, expected);
    ++t.draws;
    if (expected.pixels == half.pixels) return;
    if (t.mismatches++ < 10) {
        std::size_t i = 0;
        while (expected.pixels[i] == half.pixels[i]) ++i;
        const int w = half.stored_width();
        std::fprintf(stderr, "  %s: %dx%d at (%d,%d) on %dx%d at half resolution%s%s: first difference at (%zu,%zu)\n",
                     where.c_str(), f.width, f.height, pl.left, pl.top, full.width, full.height,
                     mirrored ? " mirrored" : "", xform ? " translucent" : "", 2 * (i % w), 2 * (i / w));
    }
}

void check_frame(const ShapeFrame& f, const XformTable& xform, const std::string& where, Totals& t) {
    ++t.frames;
    if (f.width == 0 || f.height == 0) return;
    // Half resolution: even-sized surfaces, every placement also one pixel
    // right and one down, so that both parities of each edge are drawn.
    {
        const int w = (f.width + 9) & ~1, h = (f.height + 9) & ~1;
        Surface8 full(w, h), half = Surface8::half(w, h);
        Surface8 small_full(std::max(2, f.width / 2) & ~1, std::max(2, f.height / 2) & ~1);
        Surface8 small_half = Surface8::half(small_full.width, small_full.height);
        for (const bool mirrored : {false, true}) {
            for (const XformTable* x : {static_cast<const XformTable*>(nullptr), &xform}) {
                for (const Placement pl : placements(f.width, f.height, w, h)) {
                    for (const Placement shift : {Placement{0, 0}, Placement{1, 0}, Placement{0, 1}}) {
                        compare_half(f, full, half, {pl.left + shift.left, pl.top + shift.top}, mirrored, x, where,
                                     t);
                    }
                }
                compare_half(f, small_full, small_half, {-f.width / 4, -f.height / 4}, mirrored, x, where, t);
                compare_half(f, small_full, small_half, {-f.width / 4 + 1, -f.height / 4 + 1}, mirrored, x, where,
                             t);
            }
        }
    }
    Surface8 a(f.width + 8, f.height + 8), b(a.width, a.height);
    // Smaller than the frame: clipped on all four sides at once.
    Surface8 sa(std::max(1, f.width / 2), std::max(1, f.height / 2)), sb(sa.width, sa.height);
    for (const bool mirrored : {false, true}) {
        for (const XformTable* x : {static_cast<const XformTable*>(nullptr), &xform}) {
            for (const Placement pl : placements(f.width, f.height, a.width, a.height)) {
                compare(f, a, b, pl, mirrored, x, where, t);
            }
            compare(f, sa, sb, {-f.width / 4, -f.height / 4}, mirrored, x, where, t);
        }
    }
}

void check_shapes(const std::string& name, const std::vector<Shape>& shapes, const XformTable& xform) {
    Totals t;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        const auto& frames = shapes[i].frames();
        for (std::size_t k = 0; k < frames.size(); ++k) {
            check_frame(frames[k], xform, name + " #" + std::to_string(i) + " frame " + std::to_string(k), t);
        }
    }
    std::printf("%s: %zu frames, %zu draws, %zu mismatches; reference %.2f s, ShapeFrame::draw %.2f s\n",
                name.c_str(), t.frames, t.draws, t.mismatches, t.reference_s, t.fast_s);
    if (t.mismatches) {
        std::fprintf(stderr, "FAIL: %s: ShapeFrame::draw differs from the reference\n", name.c_str());
        ++failures;
    }
}

// Hand-made frames for cases the data may not contain: a raw run and a fill
// run that run past the frame width (other data versions have such lines),
// and a transparent run that does.
void check_synthetic(const XformTable& xform) {
    static const std::uint8_t raw[] = {1, 6, 8, 9, 10, 11, 12, 13, 0, 3, 20, 21, 22, 1};
    static const std::uint8_t fill[] = {0, (5 << 1) | 1, 9, 2, (1 << 1) | 1, 30, 9};
    ShapeFrame a;
    a.width = 4;
    a.height = 2;
    a.rle = raw;
    a.lines = {0, 8};
    ShapeFrame b;
    b.width = 4;
    b.height = 2;
    b.compressed = true;
    b.xoff = 1;
    b.yoff = 1;
    b.rle = fill;
    b.lines = {0, 3};
    Totals t;
    check_frame(a, xform, "synthetic raw overshoot", t);
    check_frame(b, xform, "synthetic fill overshoot", t);
    std::printf("synthetic: %zu frames, %zu draws, %zu mismatches\n", t.frames, t.draws, t.mismatches);
    if (t.mismatches) {
        std::fprintf(stderr, "FAIL: synthetic frames: ShapeFrame::draw differs from the reference\n");
        ++failures;
    }
}

// The shapes point into the archive's memory, so the archive must outlive them.
std::vector<Shape> parse_all(const FlexArchive& flex, ShapeLayout layout, bool skip_bad_frames = false) {
    std::vector<Shape> shapes;
    for (std::size_t i = 0; i < flex.count(); ++i) {
        const auto entry = flex.entry(i);
        shapes.push_back(entry.empty() ? Shape{} : Shape::parse(entry, layout, skip_bad_frames));
    }
    return shapes;
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path data = argc > 1 ? argv[1] : "data";
    XformTable xform;
    if (std::filesystem::exists(data / "STATIC/XFORMPAL.DAT")) xform = XformTable::load(data / "STATIC/XFORMPAL.DAT");
    check_synthetic(xform);
    if (!std::filesystem::exists(data / "STATIC/SHAPES.FLX")) {
        std::printf("shape_draw_tests: no game data at %s, data checks skipped\n", data.string().c_str());
        return failures ? 1 : 0;
    }
    try {
        const auto shapes = FlexArchive::open(data / "STATIC/SHAPES.FLX");
        check_shapes("SHAPES.FLX", parse_all(shapes, ShapeLayout::World), xform);
        const auto gumps = FlexArchive::open(data / "STATIC/GUMPS.FLX");
        check_shapes("GUMPS.FLX", parse_all(gumps, ShapeLayout::Flat2D), xform);
        const auto fonts = FlexArchive::open(data / "STATIC/FONTS.FLX");
        check_shapes("FONTS.FLX", parse_all(fonts, ShapeLayout::Flat2D, true), xform);
        const auto mouse_bytes = read_file(data / "STATIC/MOUSE.SHP");
        check_shapes("MOUSE.SHP", {Shape::parse(mouse_bytes, ShapeLayout::Flat2D)}, xform);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        ++failures;
    }
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("shape_draw_tests: all frames match the reference\n");
    return 0;
}
