#include "starfox_enhanced_renderer.h"

#include "common_rtl.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/superfx.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  kRamViewPosX = 0x00b4,
  kRamViewPosY = 0x00b6,
  kRamViewPosZ = 0x00b8,
  kGsuVanishX = 0x0034,
  kGsuVanishY = 0x0036,
};

static uint16_t ram_word(uint32_t address) {
  return (uint16_t)g_ram[address] | ((uint16_t)g_ram[address + 1] << 8);
}

static uint16_t gsu_word(uint16_t address) {
  SuperFx *fx = g_snes && g_snes->cart ? g_snes->cart->superfx : NULL;
  if (!fx || !fx->ram || address + 1 >= fx->ram_size)
    return 0;
  return (uint16_t)fx->ram[address] | ((uint16_t)fx->ram[address + 1] << 8);
}

static void put_bgra(uint8_t *pixels, size_t pitch, int width, int height,
                     int x, int y, uint8_t b, uint8_t g, uint8_t r) {
  if (!pixels || x < 0 || y < 0 || x >= width || y >= height)
    return;
  uint8_t *p = pixels + (size_t)y * pitch + (size_t)x * 4u;
  p[0] = b;
  p[1] = g;
  p[2] = r;
  p[3] = 0xff;
}

static void draw_line_h(uint8_t *pixels, size_t pitch, int width, int height,
                        int x0, int x1, int y,
                        uint8_t b, uint8_t g, uint8_t r) {
  if (y < 0 || y >= height)
    return;
  if (x0 > x1) {
    int t = x0;
    x0 = x1;
    x1 = t;
  }
  if (x0 < 0) x0 = 0;
  if (x1 >= width) x1 = width - 1;
  for (int x = x0; x <= x1; x++)
    put_bgra(pixels, pitch, width, height, x, y, b, g, r);
}

static void draw_line_v(uint8_t *pixels, size_t pitch, int width, int height,
                        int x, int y0, int y1,
                        uint8_t b, uint8_t g, uint8_t r) {
  if (x < 0 || x >= width)
    return;
  if (y0 > y1) {
    int t = y0;
    y0 = y1;
    y1 = t;
  }
  if (y0 < 0) y0 = 0;
  if (y1 >= height) y1 = height - 1;
  for (int y = y0; y <= y1; y++)
    put_bgra(pixels, pitch, width, height, x, y, b, g, r);
}

static int clamp_int(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

static void draw_probe(uint8_t *pixels, size_t pitch, int width, int height,
                       uint16_t ws_extra) {
  const int view_x = (int16_t)ram_word(kRamViewPosX);
  const int view_y = (int16_t)ram_word(kRamViewPosY);
  const int view_z = (int16_t)ram_word(kRamViewPosZ);
  const int vanish_x = (int16_t)gsu_word(kGsuVanishX);
  const int vanish_y = (int16_t)gsu_word(kGsuVanishY);
  const int native_left = (int)ws_extra;
  const int native_right = native_left + 255;
  int cx = native_left + 16 + vanish_x;
  int cy = vanish_y;

  if (cx < native_left || cx > native_right)
    cx = native_left + 128 + ((view_x >> 5) % 17);
  if (cy < 0 || cy >= height)
    cy = height / 2 + ((view_y >> 5) % 17);
  cx = clamp_int(cx, 0, width - 1);
  cy = clamp_int(cy, 0, height - 1);

  draw_line_h(pixels, pitch, width, height, cx - 5, cx + 5, cy,
              0x10, 0xff, 0xff);
  draw_line_v(pixels, pitch, width, height, cx, cy - 5, cy + 5,
              0x10, 0xff, 0xff);
  draw_line_h(pixels, pitch, width, height, native_left, native_right,
              clamp_int(cy + ((view_z >> 7) % 9) - 4, 0, height - 1),
              0x00, 0x70, 0xa0);

  if (ws_extra) {
    const int left_x = clamp_int((int)ws_extra / 2, 0, width - 1);
    const int right_x = clamp_int(width - 1 - (int)ws_extra / 2, 0, width - 1);
    const int bars[3] = {
      clamp_int(8 + ((view_x & 0xff) >> 2), 8, height - 8),
      clamp_int(8 + ((view_y & 0xff) >> 2), 8, height - 8),
      clamp_int(8 + ((view_z & 0xff) >> 2), 8, height - 8),
    };
    for (int i = 0; i < 3; i++) {
      const int x = left_x + i * 3;
      draw_line_v(pixels, pitch, width, height, x, height - 8 - bars[i] / 2,
                  height - 8, 0xff, 0xd0, 0x20);
      draw_line_v(pixels, pitch, width, height, right_x - i * 3,
                  height - 8 - bars[i] / 2, height - 8, 0xff, 0xd0, 0x20);
    }
  }
}

static void dump_bgra_bmp(const char *path, const uint8_t *pixels,
                          size_t pitch, int width, int height) {
  if (!path || !*path || !pixels || pitch < (size_t)width * 4u ||
      width <= 0 || height <= 0)
    return;
  FILE *f = fopen(path, "wb");
  if (!f)
    return;
  const uint32_t image_size = (uint32_t)width * (uint32_t)height * 4u;
  const uint32_t header_size = 14u + 40u;
  const uint32_t file_size = header_size + image_size;
  uint8_t hdr[54] = { 'B', 'M' };
  const int32_t bmp_width = width;
  const int32_t bmp_height = -height;
  const uint16_t planes = 1;
  const uint16_t bpp = 32;
  const uint32_t dib_size = 40;
  memcpy(hdr + 2, &file_size, 4);
  memcpy(hdr + 10, &header_size, 4);
  memcpy(hdr + 14, &dib_size, 4);
  memcpy(hdr + 18, &bmp_width, 4);
  memcpy(hdr + 22, &bmp_height, 4);
  memcpy(hdr + 26, &planes, 2);
  memcpy(hdr + 28, &bpp, 2);
  memcpy(hdr + 34, &image_size, 4);
  fwrite(hdr, 1, sizeof(hdr), f);
  for (int y = 0; y < height; y++)
    fwrite(pixels + (size_t)y * pitch, 1, (size_t)width * 4u, f);
  fclose(f);
}

static void maybe_dump_probe(const RtlEnhancedRendererFrame *frame) {
  static int checked;
  static const char *path;
  static int target = -1;
  static int dumped;
  if (!checked) {
    checked = 1;
    path = getenv("SNESRECOMP_ENHANCED_FRAME_BMP");
    const char *target_env = getenv("SNESRECOMP_ENHANCED_FRAME_BMP_FRAME");
    if (target_env && *target_env)
      target = atoi(target_env);
  }
  if (!path || dumped)
    return;
  extern int snes_frame_counter;
  if (target >= 0 && snes_frame_counter < target)
    return;
  dump_bgra_bmp(path, frame->pixels, frame->pitch, frame->width,
                frame->height);
  dumped = 1;
}

RtlEnhancedRenderResult StarFoxEnhancedRenderFrame(
    RtlEnhancedRendererFrame *frame) {
  if (!frame || !frame->default_renderer_done)
    return kRtlEnhancedRender_NotHandled;
  draw_probe(frame->pixels, frame->pitch, frame->width, frame->height,
             frame->widescreen_extra);
  if (g_ppu && g_ppu->renderBuffer && g_ppu->renderPitch) {
    const int ppu_width = 256 + 2 * g_ppu->extraLeftRight;
    draw_probe(g_ppu->renderBuffer, g_ppu->renderPitch, ppu_width, 224,
               g_ppu->extraLeftRight);
  }
  maybe_dump_probe(frame);
  return kRtlEnhancedRender_Handled;
}
