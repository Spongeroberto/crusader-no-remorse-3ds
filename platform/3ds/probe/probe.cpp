// Probe for the 3DS port. Reports, on the top screen and in
// sdmc:/3ds/crusader/probe.txt:
//   - console model and the memory an application gets;
//   - the largest single allocation that succeeds (the real heap ceiling);
//   - whether std::filesystem works on newlib (the engine uses it widely);
//   - SD card speed reading SHAPES.FLX: sequentially in large blocks (what
//     loading an archive whole costs) and in small reads at random offsets
//     (what loading shapes on demand would cost).
// The game data is read from romfs:/data when a personal CIA bundles it, and
// from sdmc:/3ds/crusader/data (laid out as on PC) when that folder exists;
// with both, both are measured.

#include <3ds.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kRoot = "sdmc:/3ds/crusader";
std::FILE* g_log = nullptr;

void say(const char* fmt, ...) {
    char line[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    std::fputs(line, stdout);
    if (g_log) std::fputs(line, g_log);
}

double ms_since(u64 start) {
    return static_cast<double>(svcGetSystemTick() - start) * 1000.0 / SYSCLOCK_ARM11;
}

// Largest block malloc gives, to the nearest 256 KiB. The block is written
// through a volatile pointer: an allocation that is only freed again may be
// removed by the compiler, which then reports every size as available.
std::size_t largest_allocation() {
    std::size_t lo = 0, hi = 256u << 20;
    while (hi - lo > (256u << 10)) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (void* p = std::malloc(mid)) {
            volatile char* bytes = static_cast<volatile char*>(p);
            bytes[0] = 1;
            bytes[mid - 1] = 1;
            std::free(p);
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

void report_memory() {
    bool is_new = false;
    APT_CheckNew3DS(&is_new);
    say("Model: %s 3DS\n", is_new ? "New" : "classic");
    say("App memory: %lu KiB total, %lu KiB free\n",
        static_cast<unsigned long>(osGetMemRegionSize(MEMREGION_APPLICATION) >> 10),
        static_cast<unsigned long>(osGetMemRegionFree(MEMREGION_APPLICATION) >> 10));
    // libctru hands all free application memory to the heaps at start-up,
    // so the region shows nothing free: the heaps are what is left to use.
    say("Linear heap free: %lu KiB\n", static_cast<unsigned long>(linearSpaceFree() >> 10));
    say("Largest malloc: %u KiB\n", static_cast<unsigned>(largest_allocation() >> 10));
}

void report_filesystem(const fs::path& data) {
    std::error_code error;
    if (!fs::is_directory(data, error)) {
        say("No data folder at %s\n", data.c_str());
        return;
    }
    int entries = 0;
    for (const auto& entry : fs::directory_iterator(data / "STATIC", error)) {
        (void)entry;
        ++entries;
    }
    say("std::filesystem: %d entries in STATIC%s\n", entries, error ? " (error)" : "");
}

void report_sd_speed(const fs::path& file) {
    std::error_code error;
    const auto size = fs::file_size(file, error);
    if (error) {
        say("Cannot stat %s\n", file.c_str());
        return;
    }
    std::FILE* in = std::fopen(file.c_str(), "rb");
    if (!in) {
        say("Cannot open %s\n", file.c_str());
        return;
    }
    std::setvbuf(in, nullptr, _IONBF, 0);

    // Sequential, 256 KiB blocks, up to 16 MiB.
    std::vector<unsigned char> block(256u << 10);
    const std::size_t want = std::min<std::size_t>(size, 16u << 20);
    std::size_t got = 0;
    u64 t = svcGetSystemTick();
    while (got < want) {
        const std::size_t n = std::fread(block.data(), 1, block.size(), in);
        if (n == 0) break;
        got += n;
    }
    double ms = ms_since(t);
    say("Sequential: %u KiB in %.0f ms = %.2f MiB/s\n", static_cast<unsigned>(got >> 10), ms,
        ms > 0 ? (static_cast<double>(got) / (1 << 20)) / (ms / 1000.0) : 0.0);

    // Random reads, as loading single shapes would do. Several sizes separate
    // the fixed cost of a request from the cost of the bytes.
    for (const std::size_t bytes : {4096u, 65536u, 262144u}) {
        const int reads = bytes <= 4096 ? 200 : 50;
        std::srand(1);
        t = svcGetSystemTick();
        for (int i = 0; i < reads; ++i) {
            const long offset = static_cast<long>((static_cast<u64>(std::rand()) * 4096) % (size - bytes));
            std::fseek(in, offset, SEEK_SET);
            std::fread(block.data(), 1, bytes, in);
        }
        ms = ms_since(t);
        say("Random %u KiB: %.2f ms each\n", static_cast<unsigned>(bytes >> 10), ms / reads);
    }
    std::fclose(in);
}

}  // namespace

int main() {
    gfxInitDefault();
    consoleInit(GFX_TOP, nullptr);

    std::error_code error;
    fs::create_directories(kRoot, error);
    g_log = std::fopen((std::string(kRoot) + "/probe.txt").c_str(), "w");

    say("Crusader 3DS probe\n\n");
    report_memory();
    const bool romfs = R_SUCCEEDED(romfsInit());
    const fs::path sources[] = {"romfs:/data", fs::path(kRoot) / "data"};
    bool found = false;
    for (const fs::path& data : sources) {
        if (data.string().starts_with("romfs:") && !romfs) continue;
        std::error_code missing;
        if (!fs::is_directory(data, missing)) continue;
        found = true;
        say("\n%s\n", data.c_str());
        report_filesystem(data);
        report_sd_speed(data / "STATIC" / "SHAPES.FLX");
    }
    if (!found) say("\nNo game data in romfs:/data or %s/data\n", kRoot);
    if (romfs) romfsExit();
    say("\nSaved to %s/probe.txt\nPress START to exit.\n", kRoot);
    if (g_log) std::fclose(g_log);

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
    gfxExit();
    return 0;
}
