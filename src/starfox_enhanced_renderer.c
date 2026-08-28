#include "starfox_enhanced_renderer.h"

#include "common_rtl.h"
#include "starfox_enhanced_native.h"
#include "snes/cart.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/superfx.h"

#include "stb_image_write.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  kRamViewPosX = 0x00b4,
  kRamViewPosY = 0x00b6,
  kRamViewPosZ = 0x00b8,
  kRamGameFrame = 0x1640,
  kGsuNumShapes = 0x018a,
  kGsuDlPtr = 0x0202,
  kGsuVanishX = 0x0034,
  kGsuVanishY = 0x0036,
  kObjCount = 0x46,
  kGsuDrawList = 0x1960,
  kDlSize = 30,
  kDlNext = 0x00,
  kDlRotX = 0x04,
  kDlRotY = 0x05,
  kDlRotZ = 0x06,
  kDlSFlags = 0x07,
  kDlShape = 0x08,
  kDlY = 0x10,
  kDlX = 0x12,
  kDlZ = 0x14,
  kDlColTab = 0x16,
  kDlExpCnt = 0x18,
  kDlAnimFrame = 0x19,
  kDlColFrame = 0x1a,
  kDlDepth = 0x1b,
  kDlTScrollX = 0x1c,
  kDlTScrollY = 0x1d,
  kGsuDepthColours = 0x004e,
  kGsuDepthThresholds = 0x0050,
  kShapeNull = 0x9500,
  kAsfShadowShape = 0x04,
  kAsfPartObj = 0x10,
  kAsfScaledSprite = 0x20,
  kAsfTextObj = 0x40,
};

typedef struct NativeDrawEntry {
  uint16_t shape;
  int16_t x;
  int16_t y;
  int16_t z;
  uint16_t colour_pointer;
  uint8_t pitch;
  uint8_t yaw;
  uint8_t roll;
  uint8_t flags;
  uint8_t animation_frame;
  uint8_t colour_frame;
  uint8_t object_depth_offset;
  uint8_t explosion_count;
  int8_t texture_scroll_x;
  int8_t texture_scroll_y;
} NativeDrawEntry;

typedef struct NativeDrawSnapshot {
  int valid;
  int16_t vanish_x;
  int16_t vanish_y;
  unsigned shape_count;
  unsigned entry_count;
  NativeDrawEntry entries[kObjCount];
} NativeDrawSnapshot;

typedef struct NativeRendererStats {
  unsigned entries;
  unsigned candidates;
  unsigned drawn;
  unsigned declined_native_ppu;
  unsigned vertices;
  unsigned faces;
  unsigned filled_faces;
  unsigned filled_pixels;
  unsigned lines;
  unsigned line_pixels;
} NativeRendererStats;

static NativeDrawSnapshot g_native_snapshot;

void StarFoxDrawPpuFrame(void);

static uint16_t ram_word(uint32_t address) {
  return (uint16_t)g_ram[address] | ((uint16_t)g_ram[address + 1] << 8);
}

static uint8_t ram_byte(uint32_t address) {
  return g_ram[address];
}

static uint16_t gsu_word(uint16_t address) {
  SuperFx *fx = g_snes && g_snes->cart ? g_snes->cart->superfx : NULL;
  if (!fx || !fx->ram || address + 1 >= fx->ram_size)
    return 0;
  return (uint16_t)fx->ram[address] | ((uint16_t)fx->ram[address + 1] << 8);
}

static uint8_t gsu_byte(uint16_t address) {
  SuperFx *fx = g_snes && g_snes->cart ? g_snes->cart->superfx : NULL;
  if (!fx || !fx->ram || address >= fx->ram_size)
    return 0;
  return fx->ram[address];
}

static int16_t gsu_i16(uint16_t address) {
  return (int16_t)gsu_word(address);
}

static bool draw_list_pointer_valid(uint16_t pointer, unsigned shape_count,
                                    uint32_t ram_size) {
  const unsigned max_count = shape_count <= kObjCount ? shape_count : kObjCount;
  const unsigned relative = (unsigned)pointer - kGsuDrawList;
  return pointer >= kGsuDrawList &&
         relative < max_count * kDlSize &&
         (relative % kDlSize) == 0 &&
         (uint32_t)pointer + kDlSize <= ram_size;
}

static bool draw_list_slot_valid(uint16_t pointer, uint32_t ram_size) {
  return pointer >= kGsuDrawList &&
         (uint32_t)pointer + kDlSize <= ram_size &&
         (((unsigned)pointer - kGsuDrawList) % kDlSize) == 0;
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

static void draw_debug_probe(uint8_t *pixels, size_t pitch, int width,
                             int height, uint16_t ws_extra) {
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

static bool debug_probe_enabled(void) {
  static int checked;
  static int enabled;
  if (!checked) {
    const char *env = getenv("SNESRECOMP_ENHANCED_RENDERER_DEBUG");
    enabled = env && *env && strcmp(env, "0") != 0;
    checked = 1;
  }
  return enabled != 0;
}

static bool renderer_stats_enabled(void) {
  static int checked;
  static int enabled;
  if (!checked) {
    const char *env = getenv("SNESRECOMP_ENHANCED_RENDERER_STATS");
    enabled = env && *env && strcmp(env, "0") != 0;
    checked = 1;
  }
  return enabled != 0;
}

static bool native_shape_overlay_enabled(void) {
  static int checked;
  static int enabled;
  if (!checked) {
    const char *env = getenv("SNESRECOMP_ENHANCED_NATIVE_SHAPES");
    enabled = env && *env && strcmp(env, "0") != 0;
    checked = 1;
  }
  return enabled != 0;
}

static void log_renderer_stats(const NativeRendererStats *stats,
                               uint16_t ws_extra, int width, int height) {
  if (!stats || !renderer_stats_enabled())
    return;
  extern int snes_frame_counter;
  if ((snes_frame_counter % 30) != 0)
    return;
  fprintf(stderr,
          "[starfox-native] frame=%d size=%dx%d ws_extra=%u snapshot=%u/%u "
          "entries=%u candidates=%u drawn=%u declined_ppu=%u vertices=%u faces=%u "
          "filled_faces=%u filled_pixels=%u lines=%u line_pixels=%u\n",
          snes_frame_counter, width, height, ws_extra,
          g_native_snapshot.entry_count, g_native_snapshot.shape_count,
          stats->entries, stats->candidates, stats->drawn,
          stats->declined_native_ppu, stats->vertices, stats->faces,
          stats->filled_faces, stats->filled_pixels, stats->lines,
          stats->line_pixels);
}

static void snapshot_draw_entry(uint16_t pointer) {
  NativeDrawEntry *entry =
      &g_native_snapshot.entries[g_native_snapshot.entry_count++];
  entry->flags = gsu_byte((uint16_t)(pointer + kDlSFlags));
  entry->shape = gsu_word((uint16_t)(pointer + kDlShape));
  entry->x = gsu_i16((uint16_t)(pointer + kDlX));
  entry->y = gsu_i16((uint16_t)(pointer + kDlY));
  entry->z = gsu_i16((uint16_t)(pointer + kDlZ));
  entry->pitch = gsu_byte((uint16_t)(pointer + kDlRotX));
  entry->yaw = gsu_byte((uint16_t)(pointer + kDlRotY));
  entry->roll = gsu_byte((uint16_t)(pointer + kDlRotZ));
  entry->colour_pointer = gsu_word((uint16_t)(pointer + kDlColTab));
  entry->explosion_count = gsu_byte((uint16_t)(pointer + kDlExpCnt));
  entry->animation_frame = gsu_byte((uint16_t)(pointer + kDlAnimFrame));
  entry->colour_frame = gsu_byte((uint16_t)(pointer + kDlColFrame));
  entry->object_depth_offset = gsu_byte((uint16_t)(pointer + kDlDepth));
  entry->texture_scroll_x = (int8_t)gsu_byte((uint16_t)(pointer + kDlTScrollX));
  entry->texture_scroll_y = (int8_t)gsu_byte((uint16_t)(pointer + kDlTScrollY));
}

static bool draw_entry_has_native_shape(const NativeDrawEntry *entry) {
  return entry && entry->shape != 0 && entry->shape != kShapeNull &&
         (entry->flags & (kAsfShadowShape | kAsfPartObj | kAsfScaledSprite |
                          kAsfTextObj)) == 0;
}

static void snapshot_gsu_draw_list(void) {
  Cart *cart = g_snes ? g_snes->cart : NULL;
  SuperFx *fx = cart ? cart->superfx : NULL;
  memset(&g_native_snapshot, 0, sizeof(g_native_snapshot));
  if (!cart || !cart->rom || !cart->romSize || !fx || !fx->ram ||
      !fx->ram_size)
    return;

  const unsigned shape_count = gsu_word(kGsuNumShapes);
  uint16_t pointer = gsu_word(kGsuDlPtr);
  if (!shape_count || shape_count > kObjCount ||
      !draw_list_pointer_valid(pointer, shape_count, fx->ram_size))
    return;

  g_native_snapshot.valid = 1;
  g_native_snapshot.vanish_x = (int16_t)gsu_word(kGsuVanishX);
  g_native_snapshot.vanish_y = (int16_t)gsu_word(kGsuVanishY);
  g_native_snapshot.shape_count = shape_count;
  for (unsigned visited = 0; pointer != 0 && visited < shape_count; visited++) {
    if (!draw_list_pointer_valid(pointer, shape_count, fx->ram_size))
      break;
    const uint16_t next = gsu_word((uint16_t)(pointer + kDlNext));
    snapshot_draw_entry(pointer);
    pointer = next;
  }

  if (!g_native_snapshot.entry_count) {
    for (unsigned i = 0; i < shape_count && i < kObjCount; i++) {
      const uint16_t slot = (uint16_t)(kGsuDrawList + i * kDlSize);
      if (!draw_list_slot_valid(slot, fx->ram_size))
        break;
      snapshot_draw_entry(slot);
    }
  }

  if (!g_native_snapshot.entry_count)
    memset(&g_native_snapshot, 0, sizeof(g_native_snapshot));
}

static unsigned draw_snapshot_shapes(uint8_t *pixels, size_t pitch,
                                     int width, int height, uint16_t ws_extra,
                                     NativeRendererStats *renderer_stats) {
  Cart *cart = g_snes ? g_snes->cart : NULL;
  if (!ws_extra || !g_native_snapshot.valid || !cart || !cart->rom ||
      !cart->romSize || !pixels)
    return 0;

  unsigned drawn = 0;
  for (unsigned i = 0; i < g_native_snapshot.entry_count; i++) {
    const NativeDrawEntry *entry = &g_native_snapshot.entries[i];
    if (renderer_stats)
      renderer_stats->entries++;
    if (draw_entry_has_native_shape(entry)) {
      StarFoxEnhancedNativeShapePose pose;
      StarFoxEnhancedNativeShapeStats stats;
      if (renderer_stats)
        renderer_stats->candidates++;
      memset(&pose, 0, sizeof(pose));
      pose.x = entry->x;
      pose.y = entry->y;
      pose.z = entry->z;
      pose.pitch = (uint16_t)entry->pitch << 8;
      pose.yaw = (uint16_t)entry->yaw << 8;
      pose.roll = (uint16_t)entry->roll << 8;
      pose.vanish_x = g_native_snapshot.vanish_x;
      pose.vanish_y = g_native_snapshot.vanish_y;
      pose.colour_pointer = entry->colour_pointer;
      pose.depth_colours = gsu_word(kGsuDepthColours);
      pose.depth_thresholds = gsu_word(kGsuDepthThresholds);
      pose.object_depth_offset = entry->object_depth_offset;
      pose.texture_scroll_x = entry->texture_scroll_x;
      pose.texture_scroll_y = entry->texture_scroll_y;
      const uint8_t game_frame = (uint8_t)(ram_byte(kRamGameFrame) & 0x7f);
      pose.animation_frame = (entry->animation_frame & 0x80)
                                 ? (entry->animation_frame & 0x7f)
                                 : game_frame;
      pose.colour_frame = (entry->colour_frame & 0x80)
                              ? (entry->colour_frame & 0x7f)
                              : game_frame;
      pose.widescreen_extra = ws_extra;
      if (StarFoxEnhancedDrawNativeShape(pixels, pitch, width, height,
                                         cart->rom, cart->romSize,
                                         entry->shape, &pose, &stats)) {
        drawn++;
        if (renderer_stats)
          renderer_stats->drawn++;
      }
      if (renderer_stats) {
        renderer_stats->filled_pixels += stats.visible_pixels;
        renderer_stats->faces += stats.decode_failures;
      }
    }
  }
  return drawn;
}

static void clear_frame(uint8_t *pixels, size_t pitch, int width, int height) {
  if (!pixels || width <= 0 || height <= 0)
    return;
  for (int y = 0; y < height; y++)
    memset(pixels + (size_t)y * pitch, 0, (size_t)width * 4u);
}

static void copy_stock_center(const RtlEnhancedRendererFrame *frame) {
  if (!frame || !frame->pixels || !g_ppu || !g_ppu->renderBuffer ||
      !g_ppu->renderPitch || frame->width <= 0 || frame->height <= 0)
    return;
  const int copy_width = frame->width - (int)frame->widescreen_extra >= 256
                             ? 256
                             : frame->width;
  const int dst_left =
      frame->width >= 256 + 2 * (int)frame->widescreen_extra
          ? (int)frame->widescreen_extra
          : 0;
  if (copy_width <= 0 || dst_left < 0 || dst_left + copy_width > frame->width)
    return;
  const int copy_height = frame->height < 224 ? frame->height : 224;
  for (int y = 0; y < copy_height; y++) {
    memcpy(frame->pixels + (size_t)y * frame->pitch + (size_t)dst_left * 4u,
           g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch,
           (size_t)copy_width * 4u);
  }
}

static bool native_frame_looks_suspect(const RtlEnhancedRendererFrame *frame) {
  if (!frame || !frame->pixels || frame->width <= 0 || frame->height <= 0 ||
      !frame->widescreen_extra || g_native_snapshot.valid)
    return false;

  bool seen[64];
  memset(seen, 0, sizeof(seen));
  unsigned unique = 0;
  unsigned frame_non_black = 0;
  unsigned margin_non_black = 0;
  const unsigned total = (unsigned)frame->width * (unsigned)frame->height;
  const int margin_left = (int)frame->widescreen_extra;
  const int margin_right = frame->width - (int)frame->widescreen_extra;
  const unsigned margin_total =
      (unsigned)(margin_left * 2) * (unsigned)frame->height;
  for (int y = 0; y < frame->height; y += 2) {
    const uint8_t *row = frame->pixels + (size_t)y * frame->pitch;
    for (int x = 0; x < frame->width; x += 2) {
      const uint8_t *p = row + (size_t)x * 4u;
      const uint8_t b = p[0];
      const uint8_t g = p[1];
      const uint8_t r = p[2];
      if (r < 16 && g < 16 && b < 16)
        continue;
      frame_non_black += 4;
      if (x < margin_left || x >= margin_right)
        margin_non_black += 4;
      const uint8_t key =
          (uint8_t)(((r & 0xc0u) >> 2) | ((g & 0xc0u) >> 4) |
                    ((b & 0xc0u) >> 6));
      if (!seen[key]) {
        seen[key] = true;
        unique++;
      }
    }
  }
  if (margin_total != 0 && margin_non_black > margin_total / 128u &&
      unique > 4u)
    return true;
  return frame_non_black > total / 128u && unique > 8u;
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

static void dump_bgra_png(const char *path, const uint8_t *pixels,
                          size_t pitch, int width, int height) {
  if (!path || !*path || !pixels || pitch < (size_t)width * 4u ||
      width <= 0 || height <= 0)
    return;

  uint8_t *rgba = (uint8_t *)malloc((size_t)width * (size_t)height * 4u);
  if (!rgba)
    return;
  for (int y = 0; y < height; y++) {
    const uint8_t *src = pixels + (size_t)y * pitch;
    uint8_t *dst = rgba + (size_t)y * (size_t)width * 4u;
    for (int x = 0; x < width; x++) {
      dst[x * 4 + 0] = src[x * 4 + 2];
      dst[x * 4 + 1] = src[x * 4 + 1];
      dst[x * 4 + 2] = src[x * 4 + 0];
      dst[x * 4 + 3] = 0xff;
    }
  }
  stbi_write_png(path, width, height, 4, rgba, width * 4);
  free(rgba);
}

static void maybe_dump_frame(const RtlEnhancedRendererFrame *frame) {
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
  const size_t path_len = strlen(path);
  if (path_len >= 4 && strcmp(path + path_len - 4, ".png") == 0)
    dump_bgra_png(path, frame->pixels, frame->pitch, frame->width,
                  frame->height);
  else
    dump_bgra_bmp(path, frame->pixels, frame->pitch, frame->width,
                  frame->height);
  dumped = 1;
}

RtlEnhancedRenderResult StarFoxEnhancedRenderFrame(
    RtlEnhancedRendererFrame *frame) {
  if (!frame)
    return kRtlEnhancedRender_NotHandled;
  if (frame->default_renderer_done)
    return kRtlEnhancedRender_Handled;
  const bool shape_overlay_enabled = native_shape_overlay_enabled();
  NativeRendererStats stats;
  memset(&stats, 0, sizeof(stats));
  if (shape_overlay_enabled)
    snapshot_gsu_draw_list();
  clear_frame(frame->pixels, frame->pitch, frame->width, frame->height);
  StarFoxDrawPpuFrame();
  int native_ppu_done = StarFoxEnhancedDrawNativePpuLayers(
      frame->pixels, frame->pitch, frame->width, frame->height,
      frame->widescreen_extra);
  if (native_ppu_done && native_frame_looks_suspect(frame)) {
    clear_frame(frame->pixels, frame->pitch, frame->width, frame->height);
    native_ppu_done = 0;
    stats.declined_native_ppu++;
  }
  if (!native_ppu_done)
    copy_stock_center(frame);
  if (shape_overlay_enabled && !g_native_snapshot.valid)
    snapshot_gsu_draw_list();
  unsigned drawn = 0;
  if (shape_overlay_enabled) {
    drawn = draw_snapshot_shapes(frame->pixels, frame->pitch, frame->width,
                                 frame->height, frame->widescreen_extra,
                                 &stats);
    log_renderer_stats(&stats, frame->widescreen_extra, frame->width,
                       frame->height);
  }
  if (!drawn && debug_probe_enabled()) {
    draw_debug_probe(frame->pixels, frame->pitch, frame->width, frame->height,
                     frame->widescreen_extra);
  }
  maybe_dump_frame(frame);
  return kRtlEnhancedRender_Handled;
}
