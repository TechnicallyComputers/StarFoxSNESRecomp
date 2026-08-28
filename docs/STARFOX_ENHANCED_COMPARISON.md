# Star Fox Enhanced comparison

This document records what is currently useful from
<https://github.com/kandowontu/starfox-enhanced> for StarFoxSNESRecomp and,
where applicable, for the shared snesrecomp framework.

## Current local baseline

StarFoxSNESRecomp currently boots the US v1.2 game and has basic coverage for
attract, menus, route selection, training, and gameplay. The project already has
a Super FX widescreen replay model in `src/starfox_rtl.c` and detailed notes in
`docs/TRUE_WIDESCREEN.md`. The launcher still treats widescreen as hidden until
the remaining route, spawn, culling, and HUD audits are complete.

## SMWDisX ecosystem lesson

SuperMarioWorldRecomp already keeps its SMWDisX reference checkout as local
developer context and places the useful logic in title-owned tools:

- parse upstream symbol and bank assembly files;
- compare reference labels and mnemonic boundaries against recomp metadata;
- report missing or mismatched regions first;
- only apply excludes when the developer explicitly asks for mutation.

Star Fox should follow that shape. Enhanced is pinned as a submodule for
reproducibility, but the first integration layer is still a read-only inventory
and comparison harness rather than a bulk import into shared snesrecomp code.

## Phase burndown

1. Symbol generation and inventory: complete. The pinned UltraStarFox checkout
   generated `SYMBOLS.TXT`, and `tools/starfox_enhanced_symbols.py` parsed
   13,633 entries across ROM, WRAM, Super FX RAM, and constants/direct values.

2. Reviewed symbol promotion: complete for the first pass. See
   `docs/STARFOX_ENHANCED_SYMBOLS.md`. No `recomp/*.cfg` codegen entries were
   added because same-bank `name` directives auto-promote in snesrecomp v2 and
   the generated symbol table includes many non-code labels.

3. Mod hook identification: complete for the first pass. See
   `docs/STARFOX_ENHANCED_MOD_HOOKS.md` for crosshair color, god mode, God
   Nuke, and widescreen validation hooks.

4. snesrecomp feedback: complete for this pass. The framework already has
   frame-model documentation, frame counters/fingerprints, audio trace
   counters, and debug history. No shared code change is justified yet; the
   concrete follow-up is a title-neutral presentation diagnostics facade after
   Star Fox proves the title-side hooks.

5. Feature implementation: in progress. Crosshair color, including the OBJ
   reticle and Super FX cockpit HUD color hook, God Mode, God Nuke, 16:10
   widescreen preset parsing, launcher exposure for the fixed 16:9 path,
   Enhanced-style `DisplayMode` aliases, persistent
   `ShowFPS` startup state, and the retained-frame presentation debugger have
   concrete recomp-side implementations. `PresentationFPS` supports 20/30/60
   render cadence and 90/120/240/360/480 duplicate-present scheduling.
   21:9/32:9 ultrawide modes are accepted through config after raising the
   shared renderer/Super FX caps. High-FPS transform interpolation, draggable
   per-element HUD layouts, EX mode, and Super Scope/mouse/free camera behavior
   are not implemented.

6. Validation: in progress. The tools and native build validate without
   committing generated symbol or ROM output; interactive route/boss audits are
   still required before these mods should be surfaced in the launcher.

## Portable candidates

1. Symbol inventory and comparison harness.
   `tools/starfox_enhanced_symbols.py` parses `SYMBOLS.TXT` output produced by
   Enhanced's upstream build flow. `tools/starfox_enhanced_compare.py` compares
   those names against local `recomp/*.cfg` function declarations without
   writing files.

2. Trusted widescreen mod path.
   Enhanced's separation of simulation timing from presentation reinforces the
   current approach: preserve the authoritative 20 Hz game state and expand only
   the rendering/projection path. The local implementation already has this
   core; the remaining work is validation and surfacing it as a supported mod.

3. Presentation and diagnostics.
   Enhanced exposes selectable presentation rates and a visible performance
   readout. Arbitrary high-FPS rendering does not transfer directly to a
   recomp, but the diagnostic model is useful for snesrecomp: frame pacing,
   simulated-frame counters, and hardware-event counters should be observable in
   a title-neutral way.

4. Small gameplay mods.
   Crosshair color, god mode, and God Nuke now exist as isolated palette, RAM,
   and input/object-list hooks. They remain config-only until route and boss
   validation catches up.

## Deferred or non-portable areas

- Enhanced's native C++ renderer is a translation of game-specific Super FX
  shape and rasterization behavior. It is useful as a behavioral reference, but
  not a drop-in replacement for the snesrecomp hardware model.
- RetroCPU, SDL3 integration, and native app structure are source-port
  architecture, not recomp framework components.
- Star Fox EX and patch-built outputs must remain outside this repository unless
  their availability and licensing are verified separately.

## Next validation queue

- Generate or provide `third_party/starfox-enhanced/upstream-ultrastarfox/SYMBOLS.TXT`.
- Run `python tools/starfox_enhanced_symbols.py --root .`.
- Run `python tools/starfox_enhanced_compare.py --root .`.
- Promote only reviewed names or hook points into `recomp/*.cfg` or runtime mod
  code, keeping generated ROM-derived files untracked.
