#include "starfox_rtl.h"
#include "widescreen.h"

#include <stdio.h>
#include <string.h>

#include "common_cpu_infra.h"
#include "cpu_state.h"
#include "snes/cart.h"
#include "snes/dma.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/superfx.h"

uint16 counter_global_frames;

static bool s_started;
static uint32_t s_resume_pc;
static uint64_t s_next_vblank_master;
static uint32_t s_portrait_cache[64][32];
static bool s_portrait_cache_valid;
static bool s_widescreen_mode2_line_seen;
static uint8_t s_widescreen_scene_hold;

enum {
  kSnesMasterClocksPerLine = 1364,
  kSnesLinesPerFrame = 262,
  kSnesVblankStartLine = 225,
};

static bool irq_pending(void);

/* Dialog portraits occupy PPU x=65..96, y=160..223 in the US v1.2 HUD.
 * The game updates their tiles over multiple DMA slices; with the host's
 * frame-at-a-time PPU renderer an intermediate slice can otherwise appear as
 * horizontal palette stripes for one display frame. Preserve the last fully
 * formed portrait only across that unmistakable partial-upload signature. */
static void stabilize_dialog_portrait(void) {
  const unsigned left = g_ws_extra + 65;
  unsigned horizontal_changes = 0;
  unsigned vertical_changes = 0;
  for (unsigned y = 160; y < 224; y++) {
    const uint32_t *row = (const uint32_t *)(g_ppu->renderBuffer +
                                             (size_t)y * g_ppu->renderPitch);
    for (unsigned x = left + 1; x < left + 32; x++)
      horizontal_changes += row[x] != row[x - 1];
  }
  for (unsigned x = left; x < left + 32; x++) {
    const uint32_t *first_row =
        (const uint32_t *)(g_ppu->renderBuffer +
                           (size_t)160 * g_ppu->renderPitch);
    uint32_t previous = first_row[x];
    for (unsigned y = 161; y < 224; y++) {
      const uint32_t *row = (const uint32_t *)(g_ppu->renderBuffer +
                                               (size_t)y *
                                                   g_ppu->renderPitch);
      vertical_changes += row[x] != previous;
      previous = row[x];
    }
  }

  const bool partial_upload =
      horizontal_changes < 400 && vertical_changes > 800;
  if (partial_upload && s_portrait_cache_valid) {
    for (unsigned y = 0; y < 64; y++) {
      uint32_t *row = (uint32_t *)(g_ppu->renderBuffer +
                                   (size_t)(160 + y) *
                                       g_ppu->renderPitch);
      memcpy(row + left, s_portrait_cache[y],
             sizeof(s_portrait_cache[y]));
    }
  } else if (horizontal_changes >= 400) {
    for (unsigned y = 0; y < 64; y++) {
      const uint32_t *row =
          (const uint32_t *)(g_ppu->renderBuffer +
                             (size_t)(160 + y) * g_ppu->renderPitch);
      memcpy(s_portrait_cache[y], row + left,
             sizeof(s_portrait_cache[y]));
    }
    s_portrait_cache_valid = true;
  }
}

static void schedule_first_vblank(void) {
  uint32_t delta;
  if (g_snes->vPos < kSnesVblankStartLine) {
    delta = (kSnesVblankStartLine - g_snes->vPos) *
            kSnesMasterClocksPerLine - g_snes->hPos;
  } else {
    delta = (kSnesLinesPerFrame - g_snes->vPos + kSnesVblankStartLine) *
            kSnesMasterClocksPerLine - g_snes->hPos;
  }
  s_next_vblank_master = g_cpu.master_cycles + delta;
}

static uint32_t clocks_until_timer_irq(void) {
  if ((!g_snes->hIrqEnabled && !g_snes->vIrqEnabled) || g_snes->inIrq)
    return UINT32_MAX;

  const uint32_t line_clocks = kSnesMasterClocksPerLine;
  const uint32_t frame_clocks = line_clocks * kSnesLinesPerFrame;
  const uint32_t target_h = g_snes->hIrqEnabled
                                ? (uint32_t)g_snes->hTimer * 4u
                                : 0u;
  if (target_h >= line_clocks)
    return UINT32_MAX;

  if (!g_snes->vIrqEnabled) {
    uint32_t delta = target_h >= g_snes->hPos
                         ? target_h - g_snes->hPos
                         : line_clocks - g_snes->hPos + target_h;
    return delta + 1;
  }

  if (g_snes->vTimer >= kSnesLinesPerFrame)
    return UINT32_MAX;
  const uint32_t current = (uint32_t)g_snes->vPos * line_clocks +
                           g_snes->hPos;
  uint32_t target = (uint32_t)g_snes->vTimer * line_clocks + target_h;
  if (target < current)
    target += frame_clocks;
  return target - current + 1;
}

/* Advance idle hardware toward this host frame's vblank, but stop at a CPU
 * timer IRQ so the interrupted code can run at the correct beam position. */
static bool idle_hardware_toward_vblank(void) {
  if (!s_next_vblank_master)
    schedule_first_vblank();
  while (g_cpu.master_cycles < s_next_vblank_master) {
    uint64_t remaining = s_next_vblank_master - g_cpu.master_cycles;
    uint32_t chunk = remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
    uint32_t irq_delta = clocks_until_timer_irq();
    if (irq_delta < chunk)
      chunk = irq_delta;
    g_cpu.master_cycles += chunk;
    snes_advance_master_cycles(g_snes, chunk);
    cart_sync_coprocessors(g_snes->cart, g_cpu.master_cycles);
    if (irq_pending() && !g_cpu._flag_I)
      return false;
  }
  do {
    s_next_vblank_master +=
        (uint64_t)kSnesMasterClocksPerLine * kSnesLinesPerFrame;
  } while (s_next_vblank_master <= g_cpu.master_cycles);
  return true;
}

static uint16_t vector16(uint16_t address) {
  return (uint16_t)cart_read(g_snes->cart, 0, address) |
         ((uint16_t)cart_read(g_snes->cart, 0, (uint16_t)(address + 1)) << 8);
}

static void run_interrupt(bool nmi) {
  const bool emu = g_cpu.emulation != 0;
  uint16_t va = nmi ? (emu ? 0xfffa : 0xffea)
                    : (emu ? 0xfffe : 0xffee);
  uint16_t target = vector16(va);
  cpu_push_interrupt_frame(&g_cpu);
  if (!interp_bridge_run_interrupt(&g_cpu, target))
    fprintf(stderr, "[starfox] interrupt LLE bailed at $00:%04X\n", target);
}

static bool irq_pending(void) {
  SuperFx *fx = g_snes->cart->superfx;
  return g_snes->inIrq || (fx && fx->irq_pending);
}

static bool starfox_hud_active(void) {
  const SuperFx *fx = g_snes->cart->superfx;
  return fx && fx->ram_size > 0x21d &&
         (fx->ram[0x21c] | fx->ram[0x21d]);
}

static bool starfox_widescreen_scene_active(const Ppu *ppu) {
  return starfox_hud_active() || PPU_mode(ppu) == 2 ||
         s_widescreen_scene_hold != 0;
}

/* Insert the presentation-only wide GSU replay before final PPU composition.
 * This is deliberately a priority-buffer operation rather than an RGB copy:
 * the normal SNES color window, fixed-color math, brightness, subscreen, and
 * flash behavior then apply to the native center and the new side frustums as
 * one picture. */
static void starfox_widescreen_line_enhancer(Ppu *ppu, uint y, bool sub,
                                             void *context) {
  (void)context;
  if (!g_ws_extra || y == 0 || y > 160 ||
      !starfox_widescreen_scene_active(ppu) ||
      !(ppu->screenEnabled[sub] & 1))
    return;

  const uint8_t *pixels = NULL;
  const uint8_t *valid = NULL;
  const uint8_t *bg1_palette = PpuGetMode2Bg1Palette(ppu);
  unsigned width = 0, height = 0;
  if (!superfx_get_widescreen_frame(g_snes->cart->superfx, &pixels, &valid,
                                    &width, &height) ||
      y > height)
    return;

  if (PPU_mode(ppu) == 2)
    s_widescreen_mode2_line_seen = true;
  const unsigned screen_y = y - 1;
  const unsigned native_width = 224;
  const unsigned replay_extra = (unsigned)g_ws_extra + 16;
  const unsigned native_left = replay_extra;
  const unsigned native_right = native_left + native_width;
  const unsigned output_width = 256u + 2u * (unsigned)g_ws_extra;
  const unsigned capture_base = (native_width - replay_extra) / 2;

  for (unsigned raw_x = 0; raw_x < width && raw_x < output_width; raw_x++) {
    if ((raw_x >= native_left && raw_x < native_right) ||
        !valid[(size_t)screen_y * width + raw_x])
      continue;

    const unsigned side_x = raw_x < replay_extra
                                ? raw_x
                                : raw_x - native_width - replay_extra;
    const unsigned palette_x = 16 + capture_base + side_x;
    const uint8_t palette_base =
        bg1_palette[(size_t)screen_y * 256 + palette_x];
    const uint8_t palette_pixel =
        palette_base + pixels[(size_t)screen_y * width + raw_x];
    const int screen_x = (int)raw_x - g_ws_extra;
    PpuZbufType *dst =
        &ppu->bgBuffers[sub].data[screen_x + kPpuExtraLeftRight];
    const PpuZbufType replay_pixel = 0xc000u + palette_pixel;
    if (replay_pixel > *dst)
      *dst = replay_pixel;
  }
}

static bool run_main_until_boundary(void) {
  interp_bridge_set_master_deadline(s_next_vblank_master);
  const bool completed =
      interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc) != 0;
  interp_bridge_set_master_deadline(0);
  if (!completed) {
    fprintf(stderr, "[starfox] main LLE bailed at $%06X\n",
            (unsigned)s_resume_pc);
    return false;
  }
  {
    uint32_t next = interp_bridge_lle_resume_pc();
    if (next) s_resume_pc = next;
  }
  return true;
}

static void service_irq(void) {
  g_snes->inIrq = true;
  run_interrupt(false);
  g_snes->inIrq = false;
}

void StarFoxRunFrame(void) {
  /* Exact US v1.2 disassembly: RenderObjects is $01:AC1D; projection center X
   * and maximum X are GSU RAM $0034/$003A, with a 192-line scene. */
  /* The GSU framebuffer is 224 pixels wide and sits at PPU x=16..239. A
   * host margin of N pixels therefore needs N+16 newly projected columns on
   * each side to cover the complete output rather than leave a dead inset at
   * the new outer edge. */
  SuperFx *const superfx = g_snes->cart->superfx;
  const unsigned gsu_extra = g_ws_extra ? g_ws_extra + 16 : 0;
  superfx_set_enhancement_mode(
      superfx, g_ws_extra
                   ? kSuperFxEnhancement_WidescreenLinearProjection
                   : kSuperFxEnhancement_None);
  superfx_set_widescreen(superfx, (uint8_t)gsu_extra,
                         0x01, 0xac1d, 0x0034, 0x003a, 192);
  if (g_ws_extra) {
    /* RenderHUDFlag gates three screen-space GSU HUD passes inside
     * RenderObjects. $01BE similarly gates a native-viewport effect pass.
     * Their native results remain authoritative; replaying either pass with a
     * 398-pixel clip range turns framebuffer clears into opaque side bands. */
    static const uint16_t replay_zero_words[] = {0x01be, 0x021c};
    superfx_set_widescreen_replay_zero_words(
        superfx, replay_zero_words,
        sizeof(replay_zero_words) / sizeof(replay_zero_words[0]));
  } else {
    superfx_set_widescreen_replay_zero_words(superfx, NULL, 0);
  }

  if (!s_started) {
    cpu_state_init(&g_cpu, g_ram);
    s_resume_pc = vector16(0xfffc);
    s_started = true;
  } else {
    /* The S-CPU commonly reaches a WAI/polling quiescent point well before
     * vblank.  A host frame must not inject the next NMI immediately: the
     * beam, timers, auto-joypad unit, APU and GSU continue to run during that
     * idle interval.  In particular Star Fox relies on the GSU receiving the
     * full interval between vblanks to finish its framebuffer.  Stop at each
     * timer IRQ, however, and resume the interrupted CPU before continuing to
     * vblank; otherwise beam waits immediately after an IRQ can be skipped. */
    unsigned serviced = 0;
    for (;;) {
      if (irq_pending() && !g_cpu._flag_I) {
        if (serviced++ >= 64) {
          fprintf(stderr, "[starfox] IRQ did not deassert after 64 services\n");
          return;
        }
        service_irq();
        if (!run_main_until_boundary())
          return;
        continue;
      }
      if (idle_hardware_toward_vblank())
        break;
    }
    if (irq_pending() && !g_cpu._flag_I)
      service_irq();
    if (g_snes->nmiEnabled) {
      g_snes->inNmi = true;
      run_interrupt(true);
      g_snes->inNmi = false;
    }
  }

  if (!run_main_until_boundary())
    return;
  if (!s_next_vblank_master)
    schedule_first_vblank();
  if (counter_global_frames < 16 || (counter_global_frames % 120) == 0)
    fprintf(stderr,
            "[starfox] frame=%u resume=$%06X A=%04X X=%04X Y=%04X "
            "S=%04X P=%02X E=%u DB=%02X master=%llu\n",
            counter_global_frames, (unsigned)s_resume_pc, g_cpu.A, g_cpu.X,
            g_cpu.Y, g_cpu.S, g_cpu.P, g_cpu.emulation, g_cpu.DB,
            (unsigned long long)g_cpu.master_cycles);
  counter_global_frames++;
}

void StarFoxDrawPpuFrame(void) {
  SimpleHdma hdma[8];
  bool wide_scene = false;
  if (g_ws_extra && g_ppu->renderBuffer) {
    const size_t width = 256u + 2u * (unsigned)g_ws_extra;
    for (unsigned y = 0; y < 224; y++)
      memset(g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch, 0,
             width * sizeof(uint32_t));
  }
  /* BG2 is the Mode 2 landscape. Render its actual tilemap/offset result into
   * the added columns; keep BG1 (the native GSU framebuffer) and BG3/HUD
   * clamped until the GSU replay enhancer inserts only its valid side
   * pixels. */
  PpuSetExtraSpace(g_ppu, (uint8_t)g_ws_extra);
  PpuSetWidescreenLayerMask(g_ppu, 1u << 1);
  /* Star Fox centers its 224-pixel GSU playfield at x=16..239. Its opaque
   * BG1 edge padding must not cover the continuous BG2 landscape or the
   * presentation-only wide GSU replay. */
  PpuSetWidescreenLayerViewportInset(g_ppu, 0, 16, 16);
  if (g_ws_extra && starfox_hud_active()) {
    /* RenderHUD allocates OAM slots 0..9 consistently: bombs on the right,
     * then lives and shield on the left. Keep their original 16/24-pixel
     * edge padding while anchoring them to the expanded viewport. */
    PpuSetWsHudOamBand(g_ppu, 224, 64, 192);
    PpuSetWsHudOamShiftRange(g_ppu, 0, 10);
    PpuSetWidescreenLayerAnchorBand(g_ppu, 0, 161, 225, 64, 192);
  } else {
    PpuSetWsHudOamBand(g_ppu, 0, 0, 0);
    PpuSetWsHudOamShiftRange(g_ppu, 0, 0);
    PpuSetWidescreenLayerAnchorBand(g_ppu, 0, 0, 0, 0, 0);
  }
  PpuSetMode2LayerCapture(g_ppu, g_ws_extra ? 1 : -1);
  PpuSetWidescreenLineEnhancer(
      g_ppu, g_ws_extra ? starfox_widescreen_line_enhancer : NULL, NULL);
  s_widescreen_mode2_line_seen = false;
  dma_startDma(g_dma, g_snesrecomp_last_hdmaen, true);
  for (int ch = 0; ch < 8; ch++) SimpleHdma_Init(&hdma[ch], &g_dma->channel[ch]);

  for (int line = 0; line <= 224; line++) {
    for (int ch = 0; ch < 8; ch++) SimpleHdma_DoLine(&hdma[ch]);
    ppu_runLine(g_ppu, line);
  }

  {
    /* Exact US v1.2 disassembly: MainGameInit sets RenderHUDFlag at GSU RAM
     * $021C, and RenderObjects consumes it for full gameplay scenes. It stays
     * authoritative through damage/obstacle frames that temporarily leave
     * Mode 2. Post-render Mode 2 additionally covers the no-HUD attract
     * carrier; title and controls previews are Mode 1 with this flag clear. */
    const bool strong_scene =
        starfox_hud_active() || s_widescreen_mode2_line_seen ||
        PPU_mode(g_ppu) == 2;
    wide_scene = strong_scene || s_widescreen_scene_hold != 0;
    if (strong_scene)
      s_widescreen_scene_hold = 2;
    else if (s_widescreen_scene_hold)
      s_widescreen_scene_hold--;
  }

  if (g_ws_extra && !wide_scene) {
    const unsigned output_width = 256u + 2u * (unsigned)g_ws_extra;
    /* Bounded screens still render through the same full-width PPU setup so
     * switching modes never changes the native center. Discard only their
     * unowned side columns after classification. */
    for (int y = 0; y < 224; y++) {
      uint32_t *dst = (uint32_t *)(g_ppu->renderBuffer +
                                   (size_t)y * g_ppu->renderPitch);
      memset(dst, 0, (size_t)g_ws_extra * sizeof(*dst));
      memset(dst + g_ws_extra + 256, 0,
             (output_width - (unsigned)g_ws_extra - 256) * sizeof(*dst));
    }
  }

  if (g_ws_extra && wide_scene)
    stabilize_dialog_portrait();
  else
    s_portrait_cache_valid = false;

  /* The PPU center displayed the framebuffer uploaded before the newest GSU
   * completion. Promote that completion only after this picture so the side
   * replay joins the matching native center on the next display frame. */
  superfx_latch_widescreen_frame(g_snes->cart->superfx);
}
