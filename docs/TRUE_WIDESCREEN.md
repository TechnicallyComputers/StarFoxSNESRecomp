# Star Fox native widescreen rendering

Star Fox widescreen is no longer a Star Fox-specific modification of the stock
SNES renderer. The stock path renders the authentic 256x224 picture. Wider
Star Fox output belongs exclusively to the opt-in native renderer path behind
`EnhancedRenderer`.

## Contract

- Stock renderer: 256x224, no Star Fox Super FX replay widening, no Mode 2
  side capture, no HUD/OAM anchoring, no side-margin post-processing.
- Native renderer: owns the whole presentation framebuffer when enabled.
  `DisplayMode`/`Widescreen` only changes the effective output width when
  `EnhancedRenderer = 1`.
- Compatibility: old `WidescreenHud*` config keys may still parse, but they are
  not written by the default config path and are not consumed by Star Fox RTL.

## Current Native Path

`StarFoxEnhancedRenderFrame` handles the frame before the default presenter runs.
It clears the full target, asks `StarFoxDrawPpuFrame` for an authentic 256-wide
center fallback, copies that center into the requested native framebuffer, and
then draws decoded Star Fox shape geometry from the captured Super FX draw list.

That center fallback is intentionally bounded. It is not a widened PPU output
and it does not populate the side margins. The side columns are either native
renderer output or black.

## PC Port Crosswalk

The pinned Star Fox Enhanced PC port separates simulation from presentation:
the emulated game produces state, named draw points are intercepted, and host
renderers compose a wider framebuffer from game-specific assets/state.

| PC port source | Recomp counterpart | Status |
|---|---|---|
| `src/simulation/wdc65816.cpp` symbol lookup and draw interception | `recomp/bank*.cfg` `symbol` overlay plus `StarFoxEnhancedRenderFrame` | Symbols imported; draw-list snapshot in place |
| `include/starfox/render/software_renderer.hpp` `RenderPose` | `StarFoxNativeShapePose` | Partial: position, rotation, vanish point, palette, animation |
| `src/render/software_renderer.cpp` shape transform, source projection, clipping, BSP ordering, face fill | `src/starfox_native_shape.c` | Partial: shape decode, simple transform/project, fill, edges |
| `src/render/background_renderer.cpp` BG1/BG2/BG3 native tile composition | none yet | Needed for full native scene ownership |
| `src/render/sprite_renderer.cpp`, scaled text, particles, cockpit HUD | none/partial config hooks | Needed for HUD/text/effects parity |
| timing interpolation in `tests/timing_tests.cpp` and simulation snapshots | presentation history and fixed duplicate-present scheduling | Not yet interpolation |

## Validation Rule

Any 16:9/21:9/32:9 capture with non-black garbage in the side columns is a
native renderer bug. It should be fixed in the native compositor or its Star
Fox state decode, not by re-enabling the old PPU/Super FX widescreen path.
