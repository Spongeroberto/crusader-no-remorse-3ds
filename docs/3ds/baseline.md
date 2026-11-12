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

## Next measurements

- The same bench on a classic 3DS, once a homebrew build exists.
- Memory with shapes loaded on demand.
- Audio mixing and movie decoding cost; the bench skips both.
