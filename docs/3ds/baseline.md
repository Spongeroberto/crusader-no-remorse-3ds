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

## Next measurements

- The PC bench on a classic 3DS, once the engine builds for it.
- Memory with shapes loaded per map.
- Reads from plain SD card files, for comparison with RomFS.
- Audio mixing and movie decoding cost; the bench skips both.
