# PC baseline for the 3DS port

Measured 2026-11-12 on upstream `a73ac25` with `build/bench` (`src/tools/bench.cpp`), a
Release build, data from the GOG `GAME.GOG` image (English 1.01). The bench starts a new
game, skips the briefing, then plays the first map for 1800 frames (30 s at 60 Hz),
timing `Game::update` and `Game::render` separately.

```sh
./build/bench data --frames 1800 --walk
./build/bench data --frames 1800 --fire
./build/bench data --frames 1800 --walk --size 400x240
```

Host: AMD Ryzen 9 5900X (up to 4.9 GHz), one thread.

## Results

| Run | update mean / p95 | render mean / p95 | items drawn | peak RSS |
| --- | ----------------- | ----------------- | ----------- | -------- |
| walk, 640×480 | 31 µs / 42 µs | 1.65 ms / 1.69 ms | 217 | 68.4 MiB |
| fire, 640×480 | 33 µs / 44 µs | 1.71 ms / 1.77 ms | 216 | 68.4 MiB |
| walk, 400×240 asked | 33 µs / 45 µs | 1.70 ms / 1.80 ms | 217 | 69.3 MiB |

Loading a new game takes about 45 ms; peak RSS right after loading is 58 MiB.

## Findings

**Rendering is the whole frame budget.** Game logic and Usecode cost about 2% of a
frame; rendering about 98%. Under callgrind (300 frames), `ShapeFrame::draw`
(`src/core/shape.cpp`) is 87% of all instructions executed, `build_display_list` 4%,
draw-order comparison under 1%. That is roughly 24 million instructions per frame in the
sprite blitter alone.

**On a classic 3DS that is too slow, by an estimated 3–6×.** The ARM11 at 268 MHz
retires very roughly 130–220 million instructions a second on code like this, so the
blitter alone would take about 110–180 ms a frame: 5–9 fps. This is an estimate from
x86 instruction counts, not a measurement; the bench on the console will replace it.
The blitter has obvious headroom: it decodes and bounds-checks every pixel, decodes rows
that are off screen, and does not copy runs in bulk. Speeding it up helps every platform,
so it is an upstream candidate.

**Memory is over budget, mostly from one file.** Peak RSS is about 69 MiB against the
classic 3DS's roughly 64 MiB for an application, and the RSS includes the C++ runtime and
the binary. The archives read whole into memory account for about 48 MiB of it:
`SHAPES.FLX` 40.3 MiB, `SOUND.FLX` 4.3 MiB, `FIXED.DAT` 3.6 MiB. Loading shapes on demand
with a fixed-size cache is the first memory task.

**The view cannot be smaller than 640×480.** Asking for 400×240 still renders a 640×480
picture: `Game::render` treats 640×480 as the minimum. The 3DS top screen is 400×240, so
the port needs a decision: scale the 640×480 picture down (0.5× gives 320×240 with black
bars; anything else is not a whole fraction), or show a 400×240 window of the world,
which changes what the player sees, and the combat area (640×480 around the camera) would
then reach off screen.

## Classic 3DS probe

Measured 2026-11-18 with `platform/3ds/probe`, installed as a CIA (64 MB system mode) with
the game data, movies left out, in its RomFS. Luma3DS, classic 3DS.

| Measure | Result |
| ------- | ------ |
| Application memory | 64 MiB, all handed to the heaps at start-up |
| Linear heap free | 30.5 MiB |
| Largest single `malloc` | 31.25 MiB |
| `std::filesystem` on newlib | works (lists `STATIC`) |
| RomFS sequential read, 256 KiB blocks | 8.65 MiB/s |
| RomFS random read, 4 KiB | 8.23 ms |
| RomFS random read, 64 KiB | 16.96 ms |
| RomFS random read, 256 KiB | 35.63 ms |

**Memory is split in two halves.** libctru gives the 64 MiB to a linear heap (memory the
GPU can read, for framebuffers and textures) and the ordinary `malloc` heap, about 31 MiB
each. The engine's 69 MiB on PC does not fit in either half. The split is a choice:
a program can set `__ctru_linear_heap_size`, and a 400×240 game picture needs only a few
MiB of linear memory, so the `malloc` heap can grow to about 55 MiB in the 64 MB mode.
The CIA's 80 MB and 96 MB system modes add more, at the cost of system features.

**Every read costs about 8 ms before any data moves.** The random reads fit
*time ≈ 7.7 ms + size ÷ 9 MiB/s*. Small reads waste almost all their time: 4 KiB reads
deliver 0.5 MiB/s, 256 KiB reads 7 MiB/s. Loading single shapes from the file while
playing would stall (100 shapes ≈ 0.8 s). Shapes must be read in few, large, sorted
requests: the shapes a map uses at map load, the rest in bulk or on a loading screen.
Loading all of `SHAPES.FLX` (40.3 MiB) would take about 5 s, but does not fit in memory.

The probe also reads from `sdmc:/3ds/crusader/data` when that folder exists; it was
absent, so SD card files outside the CIA are not measured yet.

## Classic 3DS bench

Measured 2026-11-18 with `platform/3ds/bench`: the same loop as `build/bench`
(`src/tools/bench_run.cpp`), 600 frames at 640×480 with the player walking, Release build
(devkitARM GCC 16.1, `-O2`). Installed as a CIA (64 MB system mode) with the game data,
movies left out, in its RomFS. `__ctru_linear_heap_size` set to 4 MiB, which gave a
`malloc` heap of 58.5 MiB.

| Measure | Classic 3DS | PC (walk, 640×480) | Ratio |
| ------- | ----------- | ------------------ | ----- |
| update mean / p95 | 7.13 ms / 7.49 ms | 31 µs / 42 µs | ~230× |
| update max | 93.3 ms | | |
| render mean / p95 | 169.5 ms / 170.0 ms | 1.65 ms / 1.69 ms | ~100× |
| items drawn | 216 | 217 | |
| load (new game) | 6.8 s | 45 ms | |
| heap after load / peak | 52.5 MiB / 55.3 MiB | 58 / 68.4 MiB RSS | |
| Usecode faults | 0 | 0 | |

**The engine runs correctly on the console, at about 5.6 fps.** A frame takes about
177 ms, 96% of it rendering. The earlier estimate of 110–180 ms for the blitter held.
No Usecode faults, and the same number of items is drawn as on PC.

**Rendering must get about 6.5× faster for 30 fps.** At 30 fps a frame has 33 ms; update
takes 7 ms of it, which leaves 26 ms for rendering, against 170 ms now. 60 fps would
need rendering under 10 ms (17×) and is not a goal yet. Drawing a 400×240 view instead
of 640×480 would cut the pixels to under a third.

**Game logic is no longer negligible.** At 7 ms, update is 43% of a 60 fps frame and 21%
of a 30 fps one. Its ratio to the PC (230×) is worse than rendering's (100×), so it is
worth profiling once rendering is fixed.

**The memory fits, with 3 MiB to spare.** The peak of 55.3 MiB is below the 58.5 MiB heap,
because the 4 MiB linear heap freed most of the 64 MiB. The margin is too thin for audio,
movies and a real display, so loading shapes per map is still needed.

**Saving must go elsewhere.** The game autosaves into `<data>/SAVEGAME`, which is
read-only in RomFS. The write failed at about frame 60; that frame is the 93 ms update.
The port needs a save folder on the SD card.

**Loading takes 6.8 s**, mostly reading `SHAPES.FLX` (40.3 MiB) whole, at the 8.65 MiB/s
measured by the probe.

## Classic 3DS bench, faster blitter

Measured 2026-11-18, same build and run as above plus the `ShapeFrame::draw` rewrite
(clipping once per call, whole runs copied or filled). Its output is pixel-identical to
the old blitter (`e2e/shape_draw_tests.cpp`, 2.8 million draws).

| Measure | Old blitter | New blitter | |
| ------- | ----------- | ----------- | - |
| render mean / p95 | 169.5 ms / 170.0 ms | 52.6 ms / 53.0 ms | 3.2× faster |
| update mean / p95 | 7.13 ms / 7.49 ms | 7.13 ms / 7.47 ms | |
| frame (update + render) | 176.7 ms, 5.7 fps | 59.7 ms, 16.8 fps | |
| peak heap | 55.3 MiB | 55.3 MiB | |

On PC the same change made rendering 4.1× faster; the console gained 3.2×, short of the
estimated 40 ms. For 30 fps rendering must still get about 2× faster (26 ms). The
blitter is now about 40% of the work on PC, so the rest of the render path
(`build_display_list`, the screen clear, the shape cache) matters as much as the blitter,
and drawing a 400×240 view instead of 640×480 is the largest remaining lever.

## Classic 3DS bench, shapes read per map

Measured 2026-11-18, same run as above with `Game`'s `stream_shapes` on: `SHAPES.FLX` is
no longer read whole; when a map loads, the shapes of its items, its globs and the items
already in the world are read in merged requests and the others freed. Saves now go to
`sdmc:/3ds/crusader/SAVEGAME`. On PC the same option keeps every frame identical through
two map changes (`e2e/shape_stream_tests.cpp`) and lowers peak RSS from 68.4 to 41.3 MiB.

| Measure | Whole file | Per map | |
| ------- | ---------- | ------- | - |
| heap after load / peak | 52.5 / 55.3 MiB | 11.8 / 26.9 MiB | 28 MiB freed |
| shapes held | (all, 40.2 MiB) | 441, 9.7 MiB | |
| shape file reads / one by one | | 75 / 2 | |
| load (`Game` construction) | 6.8 s | 3.0 s | |
| render mean / p95 | 52.6 / 53.0 ms | 53.1 / 54.1 ms | |
| update mean / max | 7.13 / 94 ms | 7.51 / 187 ms | |

**The engine now uses under half the heap.** About 31 MiB of the 58.5 MiB heap is free
for audio, movies and the display.

**The map's shapes are read outside `load_ms`.** The first map loads in the first game
update, before the timed frames, so the 75 reads (about 9.7 MiB) are in neither number.
From the probe's read model they take about 1.7 s; a loading screen will need to cover
them at every map change.

**The autosave now succeeds and costs a frame of about 190 ms.** There is no "cannot
write" message any more, and the slowest update (187 ms) is most likely the autosave
writing its 255 KiB to the SD card. It will need hiding (a later frame, or a thread).

## Classic 3DS bench, the 800×480 view

Measured 2026-11-19: the view chosen in `display.md`, 800×480 shown at half size, with
shapes per map and three changes that cut the display list's work on PC (shape cache as
an array, one flat dependency array, the item table walked directly). On PC those
changes lowered render at 800×480 from 497 to 424 µs, all frame hashes unchanged.

| Measure | 640×480 (before) | 800×480 |
| ------- | ---------------- | ------- |
| render mean / p95 | 53.1 / 54.1 ms | 65.2 / 65.5 ms |
| update mean / p95 / max | 7.5 / 7.6 / 187 ms | 7.6 / 8.0 / 151 ms |
| items drawn | 216 | 260 (of 8371 in the world) |
| heap peak | 26.9 MiB | 27.0 MiB |
| Usecode faults | 0 | 0 |

**The larger view costs what its size predicts; the PC savings did not show.** 800×480
draws 1.2× the items and renders 1.23× slower, so the 15% the display-list changes saved
on PC is not visible here: on the console the blitter, which is bound by memory, must
outweigh the display list more than on PC. A breakdown by render phase on the console
is needed before more optimising.

**The autosave fails when its slot exists.** Saves are written to `SAVEGAME.n.tmp` and
renamed over `SAVEGAME.n`; the 3DS file system refuses to rename onto an existing file,
so the second run's autosave failed (the first one, with no file there, succeeded).

The top screen showed the last frame at half size, as the port will look.

## Classic 3DS bench, time by phase

Measured 2026-11-19, the same 800×480 run with `Game`'s profiling clock set (milliseconds
per frame, averaged over 600 frames; read from a photo of the screen). The autosave now
succeeds with a slot already on the SD card: no rename error, and the slowest update
(179 ms) is the save being written.

| Phase | Classic 3DS | PC | Ratio |
| ----- | ----------- | -- | ----- |
| update: kernel (processes, usecode) | 0.72 | 0.005 | ~140× |
| update: scenery animation (`animate_items`) | **5.92** | 0.020 | ~300× |
| update: audio mixing | ~0.37 | 0.007 | ~50× |
| render: clear | 0.99 | 0.005 | ~200× |
| render: display list | **14.76** | 0.134 | ~110× |
| render: draw | **34.76** | 0.240 | ~145× |
| render: overlay (sprites, status bar, pickups) | **~7.99** | 0.027 | ~300× |
| render: outside `render_view` | ~6.8 | ~0.015 | |
| update / render mean | 7.46 / 65.3 | 0.034 / 0.42 | |

**Walking every item is what the classic 3DS does worst.** Both `animate_items` (scenery,
every tick) and the display list (every frame) visit all 8371 items of the world through
a hash map, to act on a few hundred. On PC that is cheap; on the console, with no L2
cache and slow memory, it costs about 20 ms a frame between them, almost a third of the
frame.

**The status bar and the cursor cost about 15 ms.** With a screen larger than 640×480,
`Game::draw_window` copies a 640×480 block out of the screen, draws into it and copies it
back, and does so twice a frame (status bar, then cursor and messages): about 1.2 MB of
copying for a few small pictures.

**The blitter is half the frame** (35 ms), the part the half-resolution drawing targets.

## Classic 3DS bench, overlays in place and items indexed by id

Measured 2026-11-19, the same run after two changes, both with every frame hash
unchanged on PC: the status bar, cursor and messages draw straight into the screen
(`Surface8::window`) instead of a 640×480 copy out and back, and the world indexes its
items by id in an array beside its hash map.

| Phase (ms per frame) | Before | After |
| -------------------- | ------ | ----- |
| update: kernel / scenery / audio | 0.72 / 5.92 / ~0.37 | 0.64 / 4.00 / 0.37 |
| render: clear / list / draw | 0.99 / 14.76 / 34.76 | 0.99 / 15.45 / 35.02 |
| render: overlay | ~7.99 | 1.30 |
| render: outside `render_view` | ~6.8 | ~0.1 |
| **update / render mean** | **7.46 / 65.3** | **5.44 / 52.9** |

**A frame now takes about 58 ms, 17 fps at 800×480** (from 73 ms, 14 fps). The copies
around the overlays are gone (14 ms), and the scenery is 2 ms cheaper.

**The display list did not get faster, and the scenery still costs 4 ms.** Both still
touch every one of the 8371 items, and the items sit in hash-map nodes scattered through
memory: each visit is a cache miss, whether it is found through the hash map or the new
index. The fix is to visit fewer items: the scenery only animates items in the fast area,
and the display list only draws items near the view.

## Next measurements

- Time to read a map's shapes at a map change.
- Reads from plain SD card files, for comparison with RomFS.
- Audio mixing and movie decoding cost; the bench skips both.
