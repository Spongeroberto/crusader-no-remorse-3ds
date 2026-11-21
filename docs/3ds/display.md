# The 3DS view

**Decision (2026-11-18): the game is drawn as an 800×480 view, shown at half size on
the 400×240 top screen.** The player sees the original's full height and 160 pixels more
width, which fills the top screen exactly.

## Why

The top screen is 400×240, wider than 640×480 in proportion (5:3 against 4:3). Upstream's
renderer already treats a screen larger than 640×480 as a larger view: the world shows
more to the left and right, the active area widens with it, and with the original combat
zone kept, enemies at the new edges behave as they did off screen in the original. The
menus stay 640×480, centred. So 800×480 needs no change to the game logic.

Options compared side by side (same scene, 400×240 each):

| Option | What is visible | Size of things | Picture |
| ------ | --------------- | -------------- | ------- |
| **800×480 at half size (chosen)** | full height, 125% of the width | 50% | even: every other pixel |
| 640×384 at 62.5% | full width, 80% of the height | 62.5% | uneven: 3 of every 8 rows and columns dropped, or blurred when averaged |
| 640×480 at half size, with bars | the original screen | 50% | even, black bars at the sides |
| 400×240 window at full size | 62% of the width, 50% of the height | 100% | original pixels, but most of the scene hidden |

640×384 at 62.5% keeps things larger, but a scale that is not a whole fraction loses
detail unevenly, which looked worse than the smaller, even picture. Things are small at
50%; that is the accepted cost.

## Consequences

- **Speed.** Drawing 800×480 and then halving it costs about 1.23× today's render on PC
  (more items in view), roughly 65 ms a frame on a classic 3DS. The plan is to draw
  straight at half resolution, every other pixel of every other line, which gives exactly
  the same picture as halving the full one, so it can be tested pixel for pixel against
  it, like the blitter.
- **Text.** Menus, dialogs and the status bar at half size have text 3 to 4 pixels high.
  They need their own treatment (the bottom screen, or a sharper scale for text).
- **Pre-scaled assets** (shapes averaged when the CIA is built) remain an option for a
  smoother picture at the same speed, at the cost of exact testing.

## Stereoscopic 3D

**Built (2026-11-21), on while the 3D slider is up.** Each pixel's height gives its
depth: the player's floor at the screen, higher floors and the tops of walls out
towards the viewer, lower floors into the screen.

**Drawing the picture twice is too slow** on a classic 3DS, so:

1. `Surface8::attach_depth` adds a byte per pixel. While it is attached, the blitter
   writes each opaque pixel's depth as it draws (translucent pixels keep what they
   cover). The colour pixels stay byte-identical: `e2e/shape_draw_tests.cpp` checks
   colours and depth against a reference, with no depth, flat depth and box depth.
2. Each item is drawn as its box (`Surface8::set_depth`): a pixel `(u, v)` from the
   item's anchor is at height `-v - |u|/2` on a front face, at most the box's height
   (then it is on the top). Walls therefore rise from the floor to the floor above
   instead of standing as flat cards at their base, which split them from the roof
   tiles. Overlays drawn with an item (weapon, marks, casings) are flat at its middle;
   menus, movies, messages and the selection frame stay at the screen.
3. The left eye shows the normal picture. The right eye is made from it in one pass
   over 400×240 (`ctr::draw_right_eye`): each pixel is shifted left by its depth (right
   below the screen), the nearer pixel kept, and gaps filled from the pixel to the
   right (what lies behind).
4. Strength: a storey is 96 high; the floor above the player's stands 4 px out at the
   slider's top, clamped at 8 px. The slider scales it.
5. With the slider at 0 the depth is detached, `gfxSet3D` is off and no right eye is
   made: the 2D picture and its cost are as before.

The screen plane is the height the player is drawn at (so it glides with him on stairs),
or the camera's when he is not in view.

Open:

- Measured cost on a classic 3DS (depth writes + right eye), not yet done.
- Diagonal edges high above the player (wall tops) show small steps in the right eye,
  as each row's shift is a whole pixel. Judge on the console.
- A slight tilt with distance (`x + y`), so the floor looks like a table seen from
  above, is not tried.
- Lime3DS shows the eyes side by side or as anaglyph (`render_3d`, `factor_3d` in
  `qt-config.ini`), which is how this was first checked.
