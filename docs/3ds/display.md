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

## Stereoscopic 3D (idea, not started)

The top screen can show stereo 3D (every model except the 2DS). Each item's height `z`
gives a natural depth: the player's floor at the screen, higher floors and tall objects
out towards the viewer, lower floors into the screen, so stairs and multi-level rooms get
real depth.

**Drawing the picture twice is too slow** on a classic 3DS: rendering is already most of
the frame. The plan instead:

1. The blitter writes each item's depth into a one-byte-per-pixel depth buffer as it draws
   (a small cost per pixel). The colour pixels must stay byte-identical, which
   `e2e/shape_draw_tests.cpp` checks.
2. The left eye shows the normal picture.
3. The right eye is made from it in one pass over 400×240: each pixel shifted sideways by
   its depth, the few gaps filled from the neighbouring pixel. Shifts are a few pixels,
   so the gaps stay tiny. Expected cost: a few milliseconds, not a second render.
4. The 3D slider scales the shift; at zero the second eye is skipped, so 2D costs nothing.

Choices to make when it is built:

- **Depth from height only, or also from distance.** Height only keeps floors flat at the
  screen; adding a slight tilt with distance (`x + y`) makes the floor look like a table
  seen from above. Start with height only, then try the tilt.
- **Which level sits at the screen:** the player's own, so the action is comfortable to
  look at.
- **The status bar and menus** stay flat at the screen.

**Prototype without the console:** Lime3DS shows stereo as anaglyph or side by side, so
the depth buffer and the eye shift can be judged in the emulator first.
