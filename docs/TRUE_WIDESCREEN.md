# Star Fox true-widescreen rendering

Star Fox does not have one framebuffer that can simply be widened. Gameplay
combines three independently positioned sources:

- BG1 contains the 224-pixel Super FX surface, centered at PPU x=16..239.
- BG2 contains the Mode 2 landscape and uses offset-per-tile scrolling.
- OAM contains several HUD elements while the lower part of BG1 contains the
  remaining HUD meters, portraits, and radio text.

## Why the old margins looked warped

The earlier presentation copied reflected pixels from the authentic BG2
scanline into the margins. Reflection preserves an edge color but reverses
motion and perspective at that edge. Meanwhile, 3D objects came from separate
Super FX replays with shifted projection centers. The reflected landscape and
the newly projected objects therefore represented different cameras, which
made objects appear to slide, detach from the ground, or travel too far.

The replay pixels were also copied as final RGB values after PPU composition.
They bypassed the SNES color window, brightness, subscreen, and fixed-color
math. Damage and explosion flashes consequently exposed hard seams at the
native viewport boundaries.

## Current rendering model

The widescreen path now:

1. Renders BG2 normally into the added PPU columns. This preserves its live
   tilemap, scrolling, priority, and color math instead of reflecting a
   completed scanline.
2. Treats BG1 x=0..15 and x=240..255 as transparent framebuffer padding.
   BG2 or a valid side-frustum polygon can therefore occupy those columns.
3. Inserts the left/right Super FX replays into the PPU priority buffers before
   composition. Native color windows, flashes, brightness, and subscreen math
   then affect the entire scene uniformly.
4. Leaves the authoritative center render and simulation untouched. The two
   shifted Super FX passes are presentation-only and are latched to the same
   displayed frame as the native center.
5. Anchors HUD OAM slots 0..9 to the widened edges. In gameplay these are
   bombs (0..2), lives (3..5), and shield text (6..9).
6. Splits the lower BG1 HUD band into left, center, and right chunks. Meters
   move with their OAM labels, while portraits and radio text stay centered.

At 16:9 the host output is 398x224. The native Super FX playfield remains
224 pixels wide, while each presentation replay contributes 87 projected
columns (`71` host-margin pixels plus the original `16`-pixel BG1 inset).

## Remaining gameplay question

The shifted `RenderObjects` passes widen projection and clipping for every
object already submitted to the Super FX object list. Live captures confirm
that objects outside the native 4:3 view are drawn in the side frustums.

This does not, by itself, prove that every level's CPU-side spawn/despawn rule
is independent of the old viewport. The available pinned disassembly is
incomplete in the middle of the relevant Super FX object loop. Long-form route
testing should therefore watch for objects that pop into existence exactly at
x=16 or x=239; any such case needs a level/object-specific spawn-bound change,
not another projection adjustment.

## Validation

- `build-ws-diag/StarFoxSNESRecomp.exe` builds with the SDL2 trace
  configuration.
- Corneria was replayed from boot through live gameplay at 398x224.
- HUD slots and lower BG1 meters were checked in render-time OAM captures.
- A ten-checkpoint gameplay sequence covered transitions and full-frame dark
  effects without a center/side color-math seam.
- `snesrecomp/tests/ppu/run.ps1` passes.
