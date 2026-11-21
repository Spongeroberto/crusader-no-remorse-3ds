# The 3DS bottom screen

**Decision (2026-11-20): in play the bottom screen shows the player's equipment; in
menus it shows the controls.** Code: `src/engine/ui/inventory_panel.{hpp,cpp}` (the
panel, platform-free) and `platform/3ds/game/main.cpp` (`BottomScreen`, the taps).

## Why

The top screen shows the 800×480 view at half size, so the status bar's numbers and
icons are 3 to 4 pixels high (`display.md`). The bottom screen has room for them at
full size, plus every weapon and item carried, so picking one no longer means
cycling with buttons. A map was considered and left out: the original has none, and
drawing one would cost render time the classic 3DS does not have.

## Look

Everything is drawn with the game's own gumps and fonts, 320×240 at 1:1:

| Part | Asset |
| ---- | ----- |
| Health and energy | status bar boxes (GUMPS.FLX shape 1, frames 2 and 3) and their bars |
| Keycards | the pickup list's keycard icon and the count held |
| 5 weapon boxes | status bar weapon box (shape 1 frame 0), weapon icon (shape 3), rounds in the weapon over clips carried (font 12), as the status bar's ammunition box |
| 15 item cells, 3×5 | two pickup list tiles each (shape 2), the status bar's item icon, count (font 12) |
| Controls (menus), frame times | the subtitle font (4), in one light grey |
| Active weapon / item | a one-pixel outline in the menus' focus green |

The slots match the game's limits: 5 weapon slots, 15 item slots (ammunition is shown
as clips in its weapon's box). Each slot keeps its place.

- **Taller rows.** The boxes and tiles are 33 pixels high, not 28, to fill the
  screen: their top and bottom halves as they are, the middle row repeated.
- **The top screen's status bar is left out** (`Game::set_status_bar_drawn`); the panel
  shows everything it did.
- **No pulsing.** The game cycles palette entries 1–14. The panel never picks them for
  its own colours, the text is redrawn in one grey (glyphs are palette entries, some
  cycled, some black under the menu palettes), and a change in those entries alone
  does not redraw the panel.
- **Backdrop.** The game's translucency over black gives black, so the panel is
  cleared to a dark grey picked from the current palette.
- **Highlight.** Drawn solid, the status bar boxes show palette-cycled colours and
  pulse; the outline follows the menus instead (green, colour 0x0C of MISC.PAL;
  0x44 is the same green in GAMEPAL.PAL).
- **No "active item" strip or use box:** they repeated what the outline shows and
  what D-pad up already does.

## Taps

A tap on a weapon or item makes it active with the game's own keys: next weapon (W)
or previous item (I), one every other frame until it is active, at most 30 (the game
ignores them while the player is busy). Previous item because going up the items never
reaches the last slot (`player_commands.hpp`). The taps replace the buttons for next
weapon (Y) and previous / next item (D-pad left / right), which are left unbound.

## Cost

The panel is drawn and copied to the single-buffered bottom screen only when what it
shows changes (equipment, bars, the palette outside the cycled entries, the
once-a-second frame times).
