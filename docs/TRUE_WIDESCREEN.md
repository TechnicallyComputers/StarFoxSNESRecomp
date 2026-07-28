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
3. Replays `RenderObjects` once with a 398-pixel projection center and clipping
   range. A linear presentation surface gives Super FX `PLOT` and `RPIX` true
   16-bit X coordinates instead of letting the hardware tile framebuffer wrap
   at 256 pixels.
4. Inserts only that replay's new side pixels into the PPU priority buffers
   before composition. Native color windows, flashes, brightness, and
   subscreen math then affect the entire scene uniformly.
5. Leaves the authoritative center render and simulation untouched. The
   coherent wide replay is presentation-only and is latched to the same
   displayed frame as the native center. In particular, the Arwing is
   projected once at the wide center instead of being duplicated by two
   shifted cameras.
6. Excludes the native-viewport HUD and effect subpasses from the replay.
   Their normal 224-pixel results remain authoritative; letting their
   framebuffer clears inherit the 398-pixel clip range produced the solid
   side bands visible in earlier captures.
7. Anchors HUD OAM slots 0..9 to the widened edges. In gameplay these are
   bombs (0..2), lives (3..5), and shield text (6..9).
8. Splits the lower BG1 HUD band into left, center, and right chunks. Meters
   move with their OAM labels, while portraits and radio text stay centered.

At 16:9 the host output is 398x224. The native Super FX playfield remains
224 pixels wide, while the presentation replay contributes 87 projected
columns on each side (`71` host-margin pixels plus the original `16`-pixel BG1
inset).

All Super FX runtime behavior is opt-in. A title must call
`superfx_set_widescreen` for a specific task and may separately provide its
replay-only word filters. With no opt-in call, the architectural PLOT/RPIX
path and framebuffer behavior are unchanged.

## Remaining gameplay question

The wide `RenderObjects` pass widens projection and clipping for every object
already submitted to the Super FX object list. Live captures confirm
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
- TCP-driven captures covered the launch tunnel, formation, close buildings,
  rings, wingmates, damage/blanking frames, and objects crossing each former
  4:3 edge.
- HUD slots and lower BG1 meters were checked in render-time OAM captures.
- A ten-checkpoint gameplay sequence covered transitions and full-frame dark
  effects without a center/side color-math seam.
- `snesrecomp/tests/ppu/run.ps1` passes.
