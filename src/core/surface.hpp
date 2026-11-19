#pragma once
// 8-bit paletted pixel buffer: the port's replacement for the original
// Surface/VPort (rows table + clip rect). The screen is one of these too.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace crusader {

struct Surface8 {
    // The picture's size. At half resolution only every other pixel of every
    // other line of it is stored: (x, y) with x and y even, at (x / 2, y / 2).
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;  // the stored pixels; empty for a window

    Surface8() = default;
    Surface8(int w, int h, std::uint8_t fill = 0)
        : width(w), height(h), pixels(static_cast<std::size_t>(w) * h, fill) {}

    // A w x h picture (both even) stored at half resolution, for screens a
    // quarter of the size: what is drawn into it is exactly the full picture
    // with every other pixel of every other line kept, at a quarter of the
    // cost. Only code that draws through at(), the stored rows (row(),
    // stored_width()) and the functions below handles it; ShapeFrame::draw
    // does.
    static Surface8 half(int w, int h, std::uint8_t fill = 0) {
        Surface8 s(w / 2, h / 2, fill);
        s.width = w;
        s.height = h;
        s.half_ = true;
        return s;
    }

    // A w x h rectangle of `s` at (x, y), drawn into in place: its rows are
    // those of `s`, nothing is copied. It must lie inside `s` (at even x and y
    // for a half-resolution `s`), and `s` must outlive it. Only for code that
    // draws through row(), at() and clear(), not through `pixels`.
    static Surface8 window(Surface8& s, int x, int y, int w, int h) {
        Surface8 v;
        v.width = w;
        v.height = h;
        v.half_ = s.half_;
        v.origin_ = s.half_ ? s.row(y / 2) + x / 2 : s.row(y) + x;
        v.stride_ = s.stride();
        return v;
    }

    bool half_resolution() const { return half_; }
    int stored_width() const { return half_ ? width / 2 : width; }
    int stored_height() const { return half_ ? height / 2 : height; }

    // Stored row y: the picture's row y, or row 2y at half resolution.
    std::uint8_t* row(int y) { return base() + static_cast<std::size_t>(y) * stride(); }
    const std::uint8_t* row(int y) const {
        return const_cast<Surface8*>(this)->base() + static_cast<std::size_t>(y) * stride();
    }

    // The pixel (x, y) of the picture, null when it is off the surface or,
    // at half resolution, not stored.
    std::uint8_t* at(int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) return nullptr;
        if (!half_) return row(y) + x;
        if ((x | y) & 1) return nullptr;
        return row(y / 2) + x / 2;
    }

    void clear(std::uint8_t color) {
        if (!origin_) {
            std::fill(pixels.begin(), pixels.end(), color);
            return;
        }
        for (int y = 0; y < stored_height(); ++y) std::fill_n(row(y), stored_width(), color);
    }

private:
    std::uint8_t* base() { return origin_ ? origin_ : pixels.data(); }
    int stride() const { return origin_ ? stride_ : stored_width(); }
    std::uint8_t* origin_ = nullptr;  // a window's top-left pixel in its surface
    int stride_ = 0;
    bool half_ = false;
};

// One-pixel outline of the rectangle (x0, y0)-(x1, y1), corners included,
// clipped to the surface (vport_draw_rect, 1280:06D9).
// @orig 1280:06D9
inline void outline(Surface8& s, int x0, int y0, int x1, int y1, std::uint8_t c) {
    auto put = [&](int x, int y) {
        if (std::uint8_t* p = s.at(x, y)) *p = c;
    };
    for (int x = x0; x <= x1; ++x) {
        put(x, y0);
        put(x, y1);
    }
    for (int y = y0; y <= y1; ++y) {
        put(x0, y);
        put(x1, y);
    }
}

// The rectangle (x0, y0)-(x1, y1) filled, corners included, clipped
// (vport_fill_rect, 1280:0616).
inline void fill_rect(Surface8& s, int x0, int y0, int x1, int y1, std::uint8_t c) {
    for (int y = std::max(y0, 0); y <= std::min(y1, s.height - 1); ++y) {
        for (int x = std::max(x0, 0); x <= std::min(x1, s.width - 1); ++x) {
            if (std::uint8_t* p = s.at(x, y)) *p = c;
        }
    }
}

// Lines from x0 to x1 at y, and from y0 to y1 at x, ends included, clipped
// (surface_hline 1278:0357, surface_vline 1278:028E).
inline void hline(Surface8& s, int x0, int y, int x1, std::uint8_t c) { fill_rect(s, x0, y, x1, y, c); }
inline void vline(Surface8& s, int x, int y0, int y1, std::uint8_t c) { fill_rect(s, x, y0, x, y1, c); }

// The picture of `full` with every other pixel of every other line kept, into
// `half` (a half-resolution surface of the same size): what drawing straight
// into `half` gives.
inline void keep_every_other_pixel(const Surface8& full, Surface8& half) {
    for (int y = 0; y < half.stored_height(); ++y) {
        const std::uint8_t* in = full.row(2 * y);
        std::uint8_t* out = half.row(y);
        for (int x = 0; x < half.stored_width(); ++x) out[x] = in[2 * x];
    }
}

}  // namespace crusader
