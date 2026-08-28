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
It clears the full target, lets `StarFoxDrawPpuFrame` advance the game's normal
PPU/HDMA render state, renders BG1/BG2/BG3 and OAM through Star Fox Enhanced's
`BackgroundRenderer` and `SpriteRenderer` into an indexed framebuffer from raw
PPU VRAM/CGRAM/OAM/register state, and converts that framebuffer to the host
BGRA target.

The earlier local C Super FX shape overlay is not part of normal Enhanced
output and is no longer built into the Star Fox target. A provisional direct
bridge to the pinned Enhanced `SoftwareRenderer` exists behind
`SNESRECOMP_ENHANCED_NATIVE_SHAPES=1`; it decodes Star Fox ROM shapes through
Enhanced's `ShapeDecoder` and renders them through Enhanced's mesh/material
pipeline from the current GSU draw-list RAM snapshot. It remains diagnostic
until draw-order capture, object-state parity, cockpit/HUD shape coverage, and
gameplay validation are complete.

The old stock-RGB center copy remains only as a hard failure fallback if native
PPU-layer rendering cannot run or the native BG/OAM compositor detects an
unsupported wide frame with high-coverage, high-colour side-margin garbage. In
the normal Enhanced path the stock renderer is not the final image owner.

## PC Port Crosswalk

The pinned Star Fox Enhanced PC port separates simulation from presentation:
the emulated game produces state, named draw points are intercepted, and host
renderers compose a wider framebuffer from game-specific assets/state.

| PC port source | Recomp counterpart | Status |
|---|---|---|
| `src/simulation/wdc65816.cpp` symbol lookup and draw interception | `recomp/bank*.cfg` `symbol` overlay plus `StarFoxEnhancedRenderFrame` | Symbols imported; GSU draw-list snapshot has linked-head and bounded direct-slot paths |
| `include/starfox/render/software_renderer.hpp` `RenderPose` | `StarFoxEnhancedDrawNativeShape` | Provisional bridge; disabled by default pending gameplay validation |
| `src/render/software_renderer.cpp` shape transform, source projection, clipping, BSP ordering, face fill | `StarFoxEnhancedDrawNativeShape` via pinned Enhanced sources | Linked and callable; object-state/draw-order parity still pending |
| `src/render/background_renderer.cpp` BG1/BG2/BG3 native tile composition | `src/starfox_enhanced_native.cpp` | Direct Enhanced renderer bridge for native BG layers |
| `src/render/sprite_renderer.cpp`, scaled text, particles, cockpit HUD | `src/starfox_enhanced_native.cpp` for OAM only; text/effects/HUD pending | OAM bridge present; text/effects/HUD parity needed |
| timing interpolation in `tests/timing_tests.cpp` and simulation snapshots | presentation history and fixed duplicate-present scheduling | Not yet interpolation |

## Validation Rule

Any 16:9/21:9/32:9 capture with non-black garbage in the side columns is a
native renderer bug. It should be fixed in the native compositor or its Star
Fox state decode, not by re-enabling the old PPU/Super FX widescreen path.
