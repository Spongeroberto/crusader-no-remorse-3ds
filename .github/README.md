# Crusader: No Remorse — 3DS

A port of [sebsebseb1982's *Crusader: No Remorse* engine](https://github.com/sebsebseb1982/crusader-no-remorse-decomp)
to the **original Nintendo 3DS** (the classic model, not only the New 3DS).

> **Status: not running on 3DS yet.** The work has just started. Nothing here is
> playable on the console; for playing on a PC, use the
> [upstream project](https://github.com/sebsebseb1982/crusader-no-remorse-decomp).

## What this is

All the engine work — the reimplementation of the original game, its fixes and its
enhancements — is done upstream by sebsebseb1982. This fork adds what the 3DS needs:

- a 3DS platform layer (devkitARM, libctru, citro2d), with the game view on the top
  screen and the status bar, inventory and keypads on the bottom screen;
- a smaller memory footprint: the classic 3DS gives a game about 64 MB, so graphics are
  loaded when needed instead of all at once;
- speed work for the classic 3DS's 268 MHz processor, with music and sound mixed on its
  second core;
- controls for the Circle Pad, buttons and touch screen.

Changes that help every platform, such as the lower memory use, are offered back
upstream. The fork's `master` branch mirrors upstream; the 3DS work lives on the `3ds`
branch.

## Plan

1. Measure memory use and frame time of the PC build.
2. Set up the devkitPro toolchain and a first homebrew build that reads the game data
   from the SD card.
3. Draw one map on the top screen at 400×240, and measure the frame time on a real
   classic 3DS.
4. Load graphics on demand within a fixed memory budget.
5. Controls, bottom-screen HUD, sound and music, cutscenes and saves.

## Requirements (planned)

- A Nintendo 3DS, 2DS or New 3DS with homebrew enabled (Luma3DS).
- Your own copy of *Crusader: No Remorse*, in one of the versions upstream supports. This
  project contains **no game files** and never will.

## Reporting bugs

- If the problem also happens in the upstream PC build, report it
  [upstream](https://github.com/sebsebseb1982/crusader-no-remorse-decomp/issues).
- If it only happens on the 3DS, report it
  [here](https://github.com/Spongeroberto/crusader-no-remorse-3ds/issues).

## Building

There is no 3DS build yet. The PC build and the engine's own documentation are in the
[upstream README](../README.md).

## License and credits

The engine is by sebsebseb1982 and its contributors; see the
[upstream project](https://github.com/sebsebseb1982/crusader-no-remorse-decomp). This port
is released under the same license, the [GNU GPL v3](../LICENSE) or (at your option) any
later version.

*Crusader: No Remorse* remains the property of its rights holders. You must own a copy of
the game to play it.
