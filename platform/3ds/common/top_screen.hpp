#pragma once
// The game's picture on the 3DS top screen (400 x 240), shared by the game
// and the bench. A half-resolution surface (Surface8::half) of 800 x 480 is
// shown as stored; a full-size one is halved, every other pixel kept.

#include "core/palette.hpp"
#include "core/surface.hpp"

#include <3ds.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace crusader::ctr {

// Writes into the top screen's back buffer; the caller flushes and swaps.
// The framebuffer is BGR8, stored column by column, each from the bottom up,
// so the loop runs down each column to write it in order.
inline void draw_to_top_screen(const Surface8& screen, const Palette& palette) {
    u16 fb_h = 0, fb_w = 0;  // 240, 400
    u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_h, &fb_w);
    const int step = screen.half_resolution() ? 1 : 2;
    const int w = std::min<int>(fb_w, screen.stored_width() / step);
    const int h = std::min<int>(fb_h, screen.stored_height() / step);
    const int x0 = (fb_w - w) / 2, y0 = (fb_h - h) / 2;
    std::array<std::array<u8, 3>, 256> bgr;
    for (int i = 0; i < 256; ++i) bgr[i] = {palette.colors[i].b, palette.colors[i].g, palette.colors[i].r};
    for (int x = 0; x < w; ++x) {
        u8* p = fb + 3 * ((x0 + x) * fb_h + (fb_h - 1 - (y0 + h - 1)));
        for (int y = h - 1; y >= 0; --y, p += 3) {
            const auto& c = bgr[screen.row(step * y)[step * x]];
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
        }
    }
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
