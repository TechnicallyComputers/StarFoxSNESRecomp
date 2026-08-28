#include "starfox_enhanced_native.h"

extern "C" {
#include "common_rtl.h"
#include "snes/ppu.h"
}

#include "starfox/render/background_renderer.hpp"
#include "starfox/render/framebuffer.hpp"
#include "starfox/render/sprite_renderer.hpp"
#include "starfox/simulation/snes_ppu.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

enum {
  kRamBg2XScroll = 0x1723,
  kRamHdmbg2Hofs2 = 0x1934,
  kRamBg2Scroll = 0x19ca,
  kRamDoHofs = 0x19d0,
  kRamDoVofs = 0x19d1,
};

static std::uint16_t ram_word(std::uint32_t address) {
  return static_cast<std::uint16_t>(g_ram[address]) |
         static_cast<std::uint16_t>(g_ram[(address + 1u) & 0x1ffffu] << 8);
}

static std::uint16_t ram_word16(std::uint16_t address) {
  return static_cast<std::uint16_t>(g_ram[address]) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(g_ram[static_cast<std::uint16_t>(
                 address + 1u)])
             << 8u);
}

static void copy_vram_bytes(starfox::simulation::SnesPpuState &out,
                            const Ppu *ppu) {
  for (std::size_t i = 0; i < 0x8000u; i++) {
    const std::uint16_t word = ppu->vram[i];
    out.vram[i * 2u] = static_cast<std::uint8_t>(word);
    out.vram[i * 2u + 1u] = static_cast<std::uint8_t>(word >> 8);
  }
}

static void copy_oam_bytes(starfox::simulation::SnesPpuState &out,
                           const Ppu *ppu) {
  for (std::size_t i = 0; i < 0x100u; i++) {
    const std::uint16_t word = ppu->oam[i];
    out.oam[i * 2u] = static_cast<std::uint8_t>(word);
    out.oam[i * 2u + 1u] = static_cast<std::uint8_t>(word >> 8);
  }
  std::memcpy(out.oam.data() + 0x200u, ppu->highOam, 0x20u);
}

static void copy_mode2_horizontal_offsets(
    starfox::simulation::SnesPpuState &out) {
  if (!g_ram[kRamDoHofs])
    return;
  out.bg2_horizontal_offsets_enabled = true;
  const std::uint16_t source = ram_word(kRamHdmbg2Hofs2);
  for (std::size_t line = 0; line < out.bg2_horizontal_offsets.size(); line++) {
    const std::uint16_t record =
        static_cast<std::uint16_t>(source + line * 3u);
    out.bg2_horizontal_offsets[line] =
        static_cast<std::int16_t>(ram_word16(static_cast<std::uint16_t>(
            record + 1u)));
  }
}

static starfox::simulation::SnesPpuState make_ppu_state(const Ppu *ppu) {
  starfox::simulation::SnesPpuState out;
  copy_vram_bytes(out, ppu);
  std::copy(std::begin(ppu->cgram), std::end(ppu->cgram), out.cgram.begin());
  copy_oam_bytes(out, ppu);

  out.background_mode = static_cast<std::uint8_t>(PPU_mode(ppu));
  out.bg3_high_priority = PPU_bg3priority(ppu) != 0;
  out.mosaic = ppu->mosaic;
  out.object_select = ppu->obsel;
  out.bg1_character_base = static_cast<std::uint16_t>(PPU_bgTileAdr(ppu, 0));
  out.bg1_screen_base = static_cast<std::uint16_t>(PPU_bgTilemapAdr(ppu, 0));
  out.bg1_screen_size = static_cast<std::uint8_t>(ppu->bgXsc[0] & 3u);
  out.bg1_scroll_x = static_cast<std::int16_t>(ppu->hScroll[0]);
  out.bg1_scroll_y = static_cast<std::int16_t>(ppu->vScroll[0]);
  out.bg2_character_base = static_cast<std::uint16_t>(PPU_bgTileAdr(ppu, 1));
  out.bg2_screen_base = static_cast<std::uint16_t>(PPU_bgTilemapAdr(ppu, 1));
  out.bg2_screen_size = static_cast<std::uint8_t>(ppu->bgXsc[1] & 3u);
  out.bg3_character_base = static_cast<std::uint16_t>(PPU_bgTileAdr(ppu, 2));
  out.bg3_screen_base = static_cast<std::uint16_t>(PPU_bgTilemapAdr(ppu, 2));
  out.bg3_screen_size = static_cast<std::uint8_t>(ppu->bgXsc[2] & 3u);
  out.bg3_scroll_x = static_cast<std::int16_t>(ppu->hScroll[2]);
  out.bg3_scroll_y = static_cast<std::int16_t>(ppu->vScroll[2]);
  out.main_screen = ppu->screenEnabled[0];

  if (g_ram[kRamDoVofs])
    out.bg2_vertical_offsets_enabled = true;
  copy_mode2_horizontal_offsets(out);
  return out;
}

static std::uint8_t brightness(const Ppu *ppu) {
  static std::uint8_t last_visible_brightness = 15;
  const std::uint8_t level = static_cast<std::uint8_t>(ppu->inidisp & 0x0fu);
  if ((ppu->inidisp & 0x80u) == 0) {
    last_visible_brightness = level;
    return level;
  }
  return last_visible_brightness;
}

static std::uint8_t expand5(std::uint16_t value) {
  const std::uint8_t five = static_cast<std::uint8_t>(value & 0x1fu);
  return static_cast<std::uint8_t>((five << 3u) | (five >> 2u));
}

static void write_bgra(const starfox::render::Framebuffer &source,
                       const starfox::simulation::SnesPpuState &ppu_state,
                       const Ppu *ppu, std::uint8_t *pixels,
                       std::size_t pitch) {
  const std::uint8_t level = brightness(ppu);
  for (std::uint32_t y = 0; y < source.height(); y++) {
    std::uint8_t *row = pixels + static_cast<std::size_t>(y) * pitch;
    for (std::uint32_t x = 0; x < source.width(); x++) {
      const std::uint16_t cgram = ppu_state.cgram[source.get(x, y)];
      const std::uint8_t r = static_cast<std::uint8_t>(
          (static_cast<std::uint16_t>(expand5(cgram)) * level) / 15u);
      const std::uint8_t g = static_cast<std::uint8_t>(
          (static_cast<std::uint16_t>(expand5(cgram >> 5u)) * level) / 15u);
      const std::uint8_t b = static_cast<std::uint8_t>(
          (static_cast<std::uint16_t>(expand5(cgram >> 10u)) * level) / 15u);
      std::uint8_t *dst = row + static_cast<std::size_t>(x) * 4u;
      dst[0] = b;
      dst[1] = g;
      dst[2] = r;
      dst[3] = 0xff;
    }
  }
}

static std::size_t count_visible_pixels(
    const starfox::render::Framebuffer &framebuffer) {
  std::size_t count = 0;
  for (std::uint32_t y = 0; y < framebuffer.height(); y++) {
    for (std::uint32_t x = 0; x < framebuffer.width(); x++) {
      if (framebuffer.get(x, y) != 0)
        count++;
    }
  }
  return count;
}

static void maybe_log_native_ppu(const Ppu *ppu,
                                 const starfox::simulation::SnesPpuState &state,
                                 std::uint16_t widescreen_extra,
                                 std::size_t visible_pixels) {
  static int enabled = -1;
  static unsigned calls;
  if (enabled < 0) {
    const char *env = std::getenv("SNESRECOMP_ENHANCED_NATIVE_STATS");
    enabled = env && *env ? 1 : 0;
  }
  if (!enabled || (++calls % 30u) != 0u)
    return;
  std::fprintf(stderr,
               "[starfox-native-ppu] call=%u mode=%u main=%02x inidisp=%02x "
               "obsel=%02x ws_extra=%u visible=%zu bg2=(%d,%d) "
               "vofs=%u hofs=%u\n",
               calls, static_cast<unsigned>(state.background_mode),
               static_cast<unsigned>(state.main_screen),
               static_cast<unsigned>(ppu->inidisp),
               static_cast<unsigned>(state.object_select),
               static_cast<unsigned>(widescreen_extra), visible_pixels,
               static_cast<int>(ram_word(kRamBg2XScroll)),
               static_cast<int>(ram_word(kRamBg2Scroll)),
               state.bg2_vertical_offsets_enabled ? 1u : 0u,
               state.bg2_horizontal_offsets_enabled ? 1u : 0u);
}

static void draw_mode_layers(const starfox::simulation::SnesPpuState &ppu,
                             starfox::render::Framebuffer &framebuffer,
                             std::uint16_t widescreen_extra) {
  const starfox::render::BackgroundRenderer background_renderer;
  const starfox::render::SpriteRenderer sprite_renderer;
  const auto all = starfox::render::TilePriorityPass::all;
  const auto low = starfox::render::TilePriorityPass::low;
  const auto high = starfox::render::TilePriorityPass::high;
  const std::int32_t viewport_origin =
      static_cast<std::int32_t>(widescreen_extra);
  const bool extend_scene = widescreen_extra != 0 && ppu.background_mode == 2u;
  const auto bg2_scroll_x =
      static_cast<std::int16_t>(ram_word(kRamBg2XScroll));
  const auto bg2_scroll_y =
      static_cast<std::int16_t>(ram_word(kRamBg2Scroll));

  if (ppu.background_mode == 1u) {
    background_renderer.draw_bg3(ppu, framebuffer, low, viewport_origin,
                                 false);
    sprite_renderer.draw_objects(ppu, framebuffer, 0u, viewport_origin, false);
    if (!ppu.bg3_high_priority) {
      background_renderer.draw_bg3(ppu, framebuffer, high, viewport_origin,
                                   false);
    }
    sprite_renderer.draw_objects(ppu, framebuffer, 1u, viewport_origin, false);
    background_renderer.draw_bg2(ppu, bg2_scroll_x, bg2_scroll_y,
                                 framebuffer, low, viewport_origin, false);
    sprite_renderer.draw_objects(ppu, framebuffer, 2u, viewport_origin, false);
    background_renderer.draw_bg2(ppu, bg2_scroll_x, bg2_scroll_y,
                                 framebuffer, high, viewport_origin, false);
  } else if (ppu.background_mode == 2u) {
    background_renderer.draw_bg2(ppu, bg2_scroll_x, bg2_scroll_y,
                                 framebuffer, low, viewport_origin,
                                 extend_scene);
    sprite_renderer.draw_objects(ppu, framebuffer, 0u, viewport_origin,
                                 extend_scene);
    sprite_renderer.draw_objects(ppu, framebuffer, 1u, viewport_origin,
                                 extend_scene);
    background_renderer.draw_bg2(ppu, bg2_scroll_x, bg2_scroll_y, framebuffer,
                                 high, viewport_origin, extend_scene);
    sprite_renderer.draw_objects(ppu, framebuffer, 2u, viewport_origin,
                                 extend_scene);
  } else if (ppu.background_mode == 3u) {
    background_renderer.draw_bg2(ppu, bg2_scroll_x, bg2_scroll_y,
                                 framebuffer, low, viewport_origin, false);
    sprite_renderer.draw_objects(ppu, framebuffer, 0u, viewport_origin, false);
    background_renderer.draw_bg1(ppu, framebuffer, low, viewport_origin,
                                 false);
    sprite_renderer.draw_objects(ppu, framebuffer, 1u, viewport_origin, false);
    background_renderer.draw_bg2(ppu, bg2_scroll_x, bg2_scroll_y,
                                 framebuffer, high, viewport_origin, false);
    sprite_renderer.draw_objects(ppu, framebuffer, 2u, viewport_origin, false);
    background_renderer.draw_bg1(ppu, framebuffer, high, viewport_origin,
                                 false);
  } else {
    background_renderer.draw_bg2(ppu, bg2_scroll_x, bg2_scroll_y,
                                 framebuffer, all, viewport_origin,
                                 extend_scene);
    background_renderer.draw_bg3(ppu, framebuffer, all, viewport_origin,
                                 false);
    for (std::uint8_t priority = 0; priority < 3u; priority++) {
      sprite_renderer.draw_objects(ppu, framebuffer, priority, viewport_origin,
                                   extend_scene);
    }
  }
}

} // namespace

extern "C" int StarFoxEnhancedDrawNativePpuLayers(uint8_t *pixels,
                                                  size_t pitch, int width,
                                                  int height,
                                                  uint16_t widescreen_extra) {
  if (!g_ppu || !pixels || pitch < static_cast<size_t>(width) * 4u ||
      width <= 0 || height <= 0)
    return 0;

  const auto ppu_state = make_ppu_state(g_ppu);
  starfox::render::Framebuffer framebuffer(static_cast<std::uint32_t>(width),
                                           static_cast<std::uint32_t>(height));
  framebuffer.clear(0);
  draw_mode_layers(ppu_state, framebuffer, widescreen_extra);
  const auto visible_pixels = count_visible_pixels(framebuffer);
  maybe_log_native_ppu(g_ppu, ppu_state, widescreen_extra, visible_pixels);
  if (visible_pixels == 0)
    return 0;
  write_bgra(framebuffer, ppu_state, g_ppu, pixels, pitch);
  return 1;
}
