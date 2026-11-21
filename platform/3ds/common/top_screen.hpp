#pragma once
// The game's picture on the 3DS top screen (400 x 240), shared by the game
// and the bench. A half-resolution surface (Surface8::half) of 800 x 480 is
// shown as stored; a full-size one is halved, every other pixel kept. In
// stereoscopic 3D the right eye is made from it and its depth.

#include "core/palette.hpp"
#include "core/surface.hpp"

#include <3ds.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace crusader::ctr {

namespace detail {

// Writes the w x h picture whose stored pixel (x, y) is pixel(x, y), centred,
// into a top-screen eye's back buffer. The framebuffer is BGR8, stored column
// by column, each from the bottom up, so the loop runs down each column to
// write it in order.
template <class Pixel>
void draw_top_eye(gfx3dSide_t eye, int w, int h, const Palette& palette, Pixel pixel) {
    u16 fb_h = 0, fb_w = 0;  // 240, 400
    u8* fb = gfxGetFramebuffer(GFX_TOP, eye, &fb_h, &fb_w);
    w = std::min<int>(w, fb_w);
    h = std::min<int>(h, fb_h);
    const int x0 = (fb_w - w) / 2, y0 = (fb_h - h) / 2;
    std::array<std::array<u8, 3>, 256> bgr;
    for (int i = 0; i < 256; ++i) bgr[i] = {palette.colors[i].b, palette.colors[i].g, palette.colors[i].r};
    for (int x = 0; x < w; ++x) {
        u8* p = fb + 3 * ((x0 + x) * fb_h + (fb_h - 1 - (y0 + h - 1)));
        for (int y = h - 1; y >= 0; --y, p += 3) {
            const auto& c = bgr[pixel(x, y)];
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
        }
    }
}

}  // namespace detail

// Writes into the top screen's (left eye's) back buffer; the caller flushes
// and swaps.
inline void draw_to_top_screen(const Surface8& screen, const Palette& palette) {
    const int step = screen.half_resolution() ? 1 : 2;
    detail::draw_top_eye(GFX_LEFT, screen.stored_width() / step, screen.stored_height() / step, palette,
                         [&](int x, int y) { return screen.row(step * y)[step * x]; });
}

// Stereoscopic 3D: the largest shift between the eyes, in top-screen pixels,
// and the shift per unit of depth (Surface8::depth_byte, two units of
// height), both at the 3D slider's top. A storey is 96 high: the floor above
// the player's stands 4 pixels out, the one below 4 in.
constexpr int kMaxEyeShift = 8;
constexpr float kEyeShiftPerDepth = 4.0f / 48;

// The right eye's back buffer (gfxSet3D(true)), made from the left eye's
// picture, `screen`, stored at the top screen's size (a half-resolution
// 800 x 480 surface) with a depth attached: each pixel shifted left by its
// depth above the screen plane (right below it) times `slider` (0..1), the
// nearer one kept where two land on the same place. The gaps a shift opens
// show what lies behind, so each takes the pixel to its right (the far side
// of a shift to the left), or, at the right edge, the left eye's last.
// `scratch` holds the shifted picture between calls.
inline void draw_right_eye(const Surface8& screen, const Palette& palette, float slider,
                           std::vector<std::uint8_t>& scratch) {
    const int w = screen.stored_width(), h = screen.stored_height();
    std::array<int, 256> shift;
    for (int d = 0; d < 256; ++d) {
        const int s = static_cast<int>((d - Surface8::kDepthScreen) * kEyeShiftPerDepth * slider +
                                       (d >= Surface8::kDepthScreen ? 0.5f : -0.5f));
        shift[d] = std::clamp(s, -kMaxEyeShift, kMaxEyeShift);
    }
    scratch.resize(static_cast<std::size_t>(w) * h);
    std::vector<std::uint8_t> nearest(w);  // depth kept at each place of the row, 0 = a gap
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* in = screen.row(y);
        const std::uint8_t* depth = screen.depth_row(y);
        std::uint8_t* out = scratch.data() + static_cast<std::size_t>(y) * w;
        std::fill(nearest.begin(), nearest.end(), 0);
        for (int x = 0; x < w; ++x) {
            const std::uint8_t d = depth ? depth[x] : Surface8::kDepthScreen;
            const int to = x - shift[d];
            if (to < 0 || to >= w || d < nearest[to]) continue;
            out[to] = in[x];
            nearest[to] = d ? d : 1;
        }
        std::uint8_t behind = in[w - 1];
        for (int x = w - 1; x >= 0; --x) {
            if (nearest[x]) behind = out[x];
            else out[x] = behind;
        }
    }
    detail::draw_top_eye(GFX_RIGHT, w, h, palette, [&](int x, int y) { return scratch[std::size_t(y) * w + x]; });
}

// Writes a 320 x 240 surface into the bottom screen's framebuffer, laid out
// as the top one's.
inline void draw_to_bottom_screen(const Surface8& screen, const Palette& palette) {
    u16 fb_h = 0, fb_w = 0;  // 240, 320
    u8* fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &fb_h, &fb_w);
    const int w = std::min<int>(fb_w, screen.stored_width());
    const int h = std::min<int>(fb_h, screen.stored_height());
    std::array<std::array<u8, 3>, 256> bgr;
    for (int i = 0; i < 256; ++i) bgr[i] = {palette.colors[i].b, palette.colors[i].g, palette.colors[i].r};
    for (int x = 0; x < w; ++x) {
        u8* p = fb + 3 * (x * fb_h + (fb_h - h));
        for (int y = h - 1; y >= 0; --y, p += 3) {
            const auto& c = bgr[screen.row(y)[x]];
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
        }
    }
}

}  // namespace crusader::ctr
