// The engine's benchmark on a 3DS: the same loop as the PC tool
// (src/tools/bench_run.cpp), headless, with its JSON on the top screen and in
// sdmc:/3ds/crusader/bench.json. Data comes from romfs:/data when the CIA
// bundles it, else from sdmc:/3ds/crusader/data; saves go to
// sdmc:/3ds/crusader/SAVEGAME.

#include "core/palette.hpp"
#include "core/surface.hpp"
#include "tools/bench_run.hpp"

#include <3ds.h>
#include <malloc.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>

// The game picture is drawn by the CPU and needs only a few MiB of memory the
// GPU can read; the rest of the 64 MiB goes to the ordinary heap, which libctru
// sizes from what is left when this is set.
extern "C" {
u32 __ctru_linear_heap_size = 4u << 20;
// The stack the CIA asks for (app.rsf StackSize); the .3dsx would otherwise
// get libctru's 32 KiB, which the music parser overflows.
u32 __stacksize__ = 0x40000;
}

namespace {

constexpr const char* kRoot = "sdmc:/3ds/crusader";

double tick_us() {
    return static_cast<double>(svcGetSystemTick()) / (SYSCLOCK_ARM11 / 1'000'000.0);
}

// The heap grows through sbrk and never gives memory back, so its size is the
// most it has needed so far.
long heap_peak_kb() {
    return static_cast<long>(mallinfo().arena / 1024);
}

// The last frame on the top screen at half size: every other pixel of every
// other line, which is what drawing straight at half resolution would give.
// The framebuffer is BGR8, stored column by column from the bottom up.
void show_half_size(const crusader::Surface8& screen, const crusader::Palette& palette) {
    u16 fb_h = 0, fb_w = 0;  // 240, 400
    u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_h, &fb_w);
    const int w = std::min<int>(fb_w, screen.width / 2), h = std::min<int>(fb_h, screen.height / 2);
    const int x0 = (fb_w - w) / 2, y0 = (fb_h - h) / 2;
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = screen.row(2 * y);
        for (int x = 0; x < w; ++x) {
            const crusader::Rgb c = palette.colors[row[2 * x]];
            u8* p = fb + 3 * ((x0 + x) * fb_h + (fb_h - 1 - (y0 + y)));
            p[0] = c.b;
            p[1] = c.g;
            p[2] = c.r;
        }
    }
    gfxFlushBuffers();
    gfxSwapBuffers();
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
    // Text on the bottom screen; the top one shows the last frame.
    consoleInit(GFX_BOTTOM, nullptr);
    gfxSetDoubleBuffering(GFX_TOP, false);
    std::printf("Crusader bench (classic 3DS)\n");
    std::printf("Heap: %lu KiB, linear free: %lu KiB\n\n", static_cast<unsigned long>(envGetHeapSize() >> 10),
                static_cast<unsigned long>(linearSpaceFree() >> 10));

    namespace fs = std::filesystem;
    const bool romfs = R_SUCCEEDED(romfsInit());
    std::error_code error;
    crusader::BenchOptions options;
    options.data = romfs && fs::is_directory("romfs:/data", error) ? fs::path("romfs:/data")
                                                                     : fs::path(kRoot) / "data";
    // RomFS is read-only: the game saves (the autosave too) on the SD card.
    options.saves = fs::path(kRoot) / "SAVEGAME";
    // SHAPES.FLX (40 MiB) does not fit beside the rest: read per map.
    options.stream_shapes = true;
    options.frames = 600;
    // The 3DS view: the original's full height and 160 pixels more width,
    // shown at half size on the 400 x 240 top screen.
    options.width = 800;
    options.height = 480;
    options.last_frame = show_half_size;
    options.walk = true;
    options.now_us = tick_us;
    options.peak_memory_kb = heap_peak_kb;
    options.progress = [](int done, int total) {
        std::printf("\rframe %d / %d", done, total);
        gfxFlushBuffers();
        gfxSwapBuffers();
    };
    std::printf("Data: %s\nLoading...\n", options.data.c_str());
    gfxFlushBuffers();
    gfxSwapBuffers();

    try {
        const std::string json = crusader::run_bench(options);
        std::printf("\n\n%s", json.c_str());
        fs::create_directories(kRoot, error);
        if (std::FILE* out = std::fopen((std::string(kRoot) + "/bench.json").c_str(), "w")) {
            std::fputs(json.c_str(), out);
            std::fclose(out);
            std::printf("Saved to %s/bench.json\n", kRoot);
        }
    } catch (const std::exception& e) {
        std::printf("\nbench failed: %s\nheap used: %ld KiB\n", e.what(), heap_peak_kb());
    }

    wait_for_start();
    if (romfs) romfsExit();
    gfxExit();
    return 0;
}
