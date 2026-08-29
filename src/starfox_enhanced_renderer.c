#include "starfox_enhanced_renderer.h"

#include "common_rtl.h"
#include "config.h"
#include "snes/cart.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/superfx.h"
#include "starfox_enhanced_native.h"

#include "stb_image_write.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  kRamAllst = 0x121d,
  kRamAlFreeLst = 0x121f,
  kRamViewPosX = 0x00c1,
  kRamViewPosY = 0x00c3,
  kRamViewPosZ = 0x00c5,
  kRamVanishX = 0x00ca,
  kRamVanishY = 0x00cc,
  kRamDepthTabPtr = 0x1259,
  kRamMat11W = 0x15d7,
  kRamWmat11W = 0x161b,
  kRamGameFrame = 0x15bb,
  kRamHudRotation = 0x154e,
  kGsuVanishX = 0x0034,
  kGsuVanishY = 0x0036,
  kGsuDepthColours = 0x004e,
  kGsuDepthThresholds = 0x0050,
  kGsuPlayerFlyMode = 0x0174,
  kGsuShadowHeight = 0x0204,
  kGsuHudColour = 0x3512,
  kGsuHudDamageFlags = 0x3514,
  kObjBase = 0x0336,
  kObjSize = 0x36,
  kObjPoolCount = 0x46,
  kObjNext = 0x00,
  kObjShape = 0x04,
  kObjFlags = 0x08,
  kObjType = 0x09,
  kObjCounter = 0x0a,
  kObjWorldX = 0x0c,
  kObjWorldY = 0x0e,
  kObjWorldZ = 0x10,
  kObjRotX = 0x12,
  kObjRotY = 0x13,
  kObjRotZ = 0x14,
  kObjSFlags = 0x1d,
  kObjSFlags2 = 0x1e,
  kObjSFlags3 = 0x1f,
  kObjSFlags4 = 0x20,
  kObjAuxDepthOffset = 0x1cdf,
  kObjAuxColourFrame = 0x1ce6,
  kObjAuxAnimationFrame = 0x1ce7,
  kObjAuxColourTable = 0x1cea,
  kObjAuxTextureScrollX = 0x1cf4,
  kObjAuxTextureScrollY = 0x1cf5,
  kRomColourWhite = 0x800c,
  kRomColourRed = 0x80fc,
  kRomColourSpecial = 0x82ed,
  kRomDepthThresholdDefault = 0x8faa,
  kShapeNull = 0xaca1,
  kAfExplosion = 0x01,
  kAtGround = 0x01,
  kPfmShadows = 0x08,
  kSourceVanishDefaultX = 112,
  kSourceVanishDefaultY = 96,
  kSuperFxHorizontalInset = 16,
  kSuperFxVerticalInset = 16,
  kAsfShadowShape = 0x04,
  kAsfShadow = 0x08,
  kAsfShadowMask = kAsfShadowShape | kAsfShadow,
  kAsfPartObj = 0x10,
  kAsfScaledSprite = 0x20,
  kAsfTextObj = 0x40,
  kAsfInvisible4 = 0x08,
  kShadowForcedColour = 0x09,
};

typedef struct NativeSourceObject {
  uint16_t pointer;
  uint16_t shape;
  uint16_t colour_pointer;
  uint16_t fire_object;
  int16_t world_x;
  int16_t world_y;
  int16_t world_z;
  int16_t camera_x;
  int16_t camera_y;
  int16_t camera_z;
  int16_t sort_depth;
  uint8_t pitch;
  uint8_t yaw;
  uint8_t roll;
  uint8_t flags;
  uint8_t type;
  uint8_t count;
  uint8_t sflags[4];
  uint8_t strategy_state;
  uint8_t animation_frame;
  uint8_t colour_frame;
  uint8_t object_depth_offset;
  uint8_t explosion_count;
  uint8_t sound1;
  uint8_t sound2;
  int8_t texture_scroll_x;
  int8_t texture_scroll_y;
} NativeSourceObject;

typedef struct NativeSourceFrameSnapshot {
  int valid;
  int frame;
  uint8_t game_frame;
  int16_t view_x;
  int16_t view_y;
  int16_t view_z;
  int16_t view_matrix[9];
  int16_t vanish_x;
  int16_t vanish_y;
  uint8_t player_fly_mode;
  int16_t shadow_height;
  uint16_t hud_rotation;
  uint8_t hud_colour;
  uint8_t hud_damage_flags;
  uint16_t depth_colours;
  uint16_t depth_thresholds;
  unsigned active_count;
  unsigned draw_count;
  unsigned unsupported_invisible;
  unsigned unsupported_shadow;
  unsigned unsupported_particle;
  unsigned unsupported_scaled;
  unsigned unsupported_text;
  unsigned unsupported_culled;
  unsigned unsupported_invalid;
  NativeSourceObject objects[kObjPoolCount];
  uint8_t draw_order[kObjPoolCount];
} NativeSourceFrameSnapshot;

typedef struct NativeRendererStats {
  unsigned entries;
  unsigned candidates;
  unsigned drawn;
  unsigned declined_native_ppu;
  unsigned unsupported_invisible;
  unsigned unsupported_shadow;
  unsigned unsupported_particle;
  unsigned unsupported_scaled;
  unsigned unsupported_text;
  unsigned unsupported_culled;
  unsigned unsupported_invalid;
  unsigned decode_failures;
  unsigned vertices;
  unsigned faces;
  unsigned filled_faces;
  unsigned filled_pixels;
  unsigned lines;
  unsigned line_pixels;
  unsigned cockpit_pixels;
  unsigned native_world_ready;
  unsigned native_world_suppressed;
} NativeRendererStats;

static NativeSourceFrameSnapshot g_source_snapshot;

enum {
  kNativeWorldMinActiveObjects = 8,
  kNativeWorldMinDrawnShapes = 2,
  kNativeWorldMinVisiblePixels = 4096,
};

void StarFoxDrawPpuFrame(void);

static uint16_t ram_word(uint32_t address) {
  return (uint16_t)g_ram[address] | ((uint16_t)g_ram[address + 1] << 8);
}

static uint8_t ram_byte(uint32_t address) { return g_ram[address]; }

static int8_t ram_i8(uint32_t address) { return (int8_t)ram_byte(address); }

static int16_t ram_i16(uint32_t address) { return (int16_t)ram_word(address); }

static uint16_t gsu_word_from(const SuperFx *fx, uint16_t address) {
  if (!fx || !fx->ram || address + 1 >= fx->ram_size)
    return 0;
  return (uint16_t)fx->ram[address] | ((uint16_t)fx->ram[address + 1] << 8);
}

static SuperFx *current_superfx(void) {
  return g_snes && g_snes->cart ? g_snes->cart->superfx : NULL;
}

static uint16_t gsu_word(uint16_t address) {
  return gsu_word_from(current_superfx(), address);
}

static uint8_t gsu_byte(uint16_t address) {
  const SuperFx *fx = current_superfx();
  if (!fx || !fx->ram || address >= fx->ram_size)
    return 0;
  return fx->ram[address];
}

static int16_t wrap16_i64(int64_t value) {
  return (int16_t)(uint16_t)(uint64_t)value;
}

static int16_t add16(int16_t left, int16_t right) {
  return wrap16_i64((int32_t)left + (int32_t)right);
}

static int16_t subtract16(int16_t left, int16_t right) {
  return wrap16_i64((int32_t)left - (int32_t)right);
}

static int32_t arithmetic_shift_right32(int32_t value, unsigned bits) {
  if (!bits)
    return value;
  if (bits >= 31)
    return value < 0 ? -1 : 0;
  if (value >= 0)
    return value >> bits;
  {
    const int64_t magnitude = -(int64_t)value;
    const int64_t divisor = (int64_t)1 << bits;
    return (int32_t)-((magnitude + divisor - 1) / divisor);
  }
}

static int16_t multiply_q15(int16_t left, int16_t right) {
  return wrap16_i64(
      arithmetic_shift_right32((int32_t)left * (int32_t)right, 15));
}

static int16_t transform_q15_component(const int16_t matrix[9], int16_t x,
                                       int16_t y, int16_t z, unsigned column) {
  int16_t result = multiply_q15(x, matrix[column]);
  result = add16(result, multiply_q15(y, matrix[3 + column]));
  return add16(result, multiply_q15(z, matrix[6 + column]));
}

static bool object_pointer_index(uint16_t pointer, unsigned *index) {
  if (pointer < kObjBase)
    return false;
  const unsigned relative = (unsigned)pointer - kObjBase;
  if (relative >= kObjPoolCount * kObjSize || (relative % kObjSize) != 0)
    return false;
  if (index)
    *index = relative / kObjSize;
  return true;
}

static bool rom_read8_lorom(const uint8_t *rom, size_t rom_size,
                            uint32_t snes_address, uint8_t *out) {
  const uint16_t address = (uint16_t)snes_address;
  if (!rom || !rom_size || address < 0x8000)
    return false;
  {
    const size_t offset = (size_t)((snes_address >> 16) & 0x7fu) * 0x8000u +
                          (size_t)(address & 0x7fffu);
    if (offset >= rom_size)
      return false;
    if (out)
      *out = rom[offset];
    return true;
  }
}

static bool rom_read_i16_lorom(const uint8_t *rom, size_t rom_size,
                               uint32_t snes_address, int16_t *out) {
  uint8_t low = 0;
  uint8_t high = 0;
  if (!rom_read8_lorom(rom, rom_size, snes_address, &low) ||
      !rom_read8_lorom(rom, rom_size, snes_address + 1, &high)) {
    return false;
  }
  if (out)
    *out = (int16_t)((uint16_t)low | ((uint16_t)high << 8));
  return true;
}

static bool read_shape_metadata(const Cart *cart, uint16_t shape,
                                int16_t *sort_z, int16_t *z_max) {
  if (!cart || !cart->rom || !cart->romSize || !shape || shape == kShapeNull)
    return false;
  return rom_read_i16_lorom(cart->rom, cart->romSize, (uint32_t)shape + 5u,
                            sort_z) &&
         rom_read_i16_lorom(cart->rom, cart->romSize, (uint32_t)shape + 14u,
                            z_max);
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
                        int x0, int x1, int y, uint8_t b, uint8_t g,
                        uint8_t r) {
  if (y < 0 || y >= height)
    return;
  if (x0 > x1) {
    int t = x0;
    x0 = x1;
    x1 = t;
  }
  if (x0 < 0)
    x0 = 0;
  if (x1 >= width)
    x1 = width - 1;
  for (int x = x0; x <= x1; x++)
    put_bgra(pixels, pitch, width, height, x, y, b, g, r);
}

static void draw_line_v(uint8_t *pixels, size_t pitch, int width, int height,
                        int x, int y0, int y1, uint8_t b, uint8_t g,
                        uint8_t r) {
  if (x < 0 || x >= width)
    return;
  if (y0 > y1) {
    int t = y0;
    y0 = y1;
    y1 = t;
  }
  if (y0 < 0)
    y0 = 0;
  if (y1 >= height)
    y1 = height - 1;
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

  draw_line_h(pixels, pitch, width, height, cx - 5, cx + 5, cy, 0x10, 0xff,
              0xff);
  draw_line_v(pixels, pitch, width, height, cx, cy - 5, cy + 5, 0x10, 0xff,
              0xff);
  draw_line_h(pixels, pitch, width, height, native_left, native_right,
              clamp_int(cy + ((view_z >> 7) % 9) - 4, 0, height - 1), 0x00,
              0x70, 0xa0);

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

static bool native_shape_diagnostics_enabled(void) {
  static int checked;
  static int enabled;
  if (!checked) {
    const char *env = getenv("SNESRECOMP_ENHANCED_NATIVE_SHAPE_DIAGNOSTICS");
    enabled = env && *env && strcmp(env, "0") != 0;
    checked = 1;
  }
  return enabled != 0;
}

static bool native_world_gate_diagnostics_enabled(void) {
  static int checked;
  static int enabled;
  if (!checked) {
    const char *env = getenv("SNESRECOMP_ENHANCED_NATIVE_WORLD_GATE_LOG");
    enabled = env && *env && strcmp(env, "0") != 0;
    checked = 1;
  }
  return enabled != 0;
}

static void log_native_shape_diagnostic(
    unsigned draw_index, const NativeSourceObject *object,
    const StarFoxEnhancedNativeShapePose *pose,
    const StarFoxEnhancedNativeShapeStats *stats, int visible) {
  if (!native_shape_diagnostics_enabled() || !object || !pose || !stats)
    return;
  extern int snes_frame_counter;
  if (draw_index == 0) {
    fprintf(stderr,
            "[starfox-native-pose] frame=%d world=(%d,%d,%d) "
            "view=(%d,%d,%d) wmat=[%d,%d,%d;%d,%d,%d;%d,%d,%d]\n",
            snes_frame_counter, (int)object->world_x, (int)object->world_y,
            (int)object->world_z, (int)g_source_snapshot.view_x,
            (int)g_source_snapshot.view_y, (int)g_source_snapshot.view_z,
            (int)g_source_snapshot.view_matrix[0],
            (int)g_source_snapshot.view_matrix[1],
            (int)g_source_snapshot.view_matrix[2],
            (int)g_source_snapshot.view_matrix[3],
            (int)g_source_snapshot.view_matrix[4],
            (int)g_source_snapshot.view_matrix[5],
            (int)g_source_snapshot.view_matrix[6],
            (int)g_source_snapshot.view_matrix[7],
            (int)g_source_snapshot.view_matrix[8]);
  }
  fprintf(stderr,
          "[starfox-native-shape] frame=%d draw_index=%u ptr=%04x "
          "shape=%04x lod=%04x camera=(%d,%d,%d) rot=(%u,%u,%u) "
          "vanish=(%d,%d) ws_extra=%u flags=%02x type=%02x count=%u "
          "sflags=%02x/%02x/%02x/%02x colour=%04x depth_ofs=%u "
          "frames=%u/%u scroll=(%d,%d) decoded=%u/%u visible=%u "
          "decode_failures=%u result=%d\n",
          snes_frame_counter, draw_index, object->pointer, object->shape,
          stats->selected_lod, (int)pose->x, (int)pose->y, (int)pose->z,
          (unsigned)pose->pitch, (unsigned)pose->yaw, (unsigned)pose->roll,
          (int)pose->vanish_x, (int)pose->vanish_y,
          (unsigned)pose->widescreen_extra, (unsigned)object->flags,
          (unsigned)object->type, (unsigned)object->count,
          (unsigned)object->sflags[0], (unsigned)object->sflags[1],
          (unsigned)object->sflags[2], (unsigned)object->sflags[3],
          (unsigned)object->colour_pointer,
          (unsigned)object->object_depth_offset, pose->colour_frame,
          pose->animation_frame, (int)pose->texture_scroll_x,
          (int)pose->texture_scroll_y, stats->decoded_vertices,
          stats->decoded_faces, stats->visible_pixels,
          stats->decode_failures, visible);
}

static void log_renderer_stats(const NativeRendererStats *stats,
                               uint16_t ws_extra, int width, int height) {
  if (!stats || !renderer_stats_enabled())
    return;
  extern int snes_frame_counter;
  if ((snes_frame_counter % 30) != 0)
    return;
  fprintf(stderr,
          "[starfox-native] frame=%d size=%dx%d ws_extra=%u "
          "source=%u/%u snap_frame=%d "
          "entries=%u candidates=%u drawn=%u ready=%u suppress=%u "
          "declined_ppu=%u "
          "unsupported inv=%u shadow=%u particle=%u scaled=%u text=%u "
          "culled=%u invalid=%u decode=%u vertices=%u faces=%u "
          "filled_faces=%u filled_pixels=%u lines=%u line_pixels=%u "
          "cockpit_pixels=%u\n",
          snes_frame_counter, width, height, ws_extra,
          g_source_snapshot.draw_count, g_source_snapshot.active_count,
          g_source_snapshot.frame, stats->entries, stats->candidates,
          stats->drawn, stats->native_world_ready,
          stats->native_world_suppressed, stats->declined_native_ppu,
          stats->unsupported_invisible, stats->unsupported_shadow,
          stats->unsupported_particle, stats->unsupported_scaled,
          stats->unsupported_text, stats->unsupported_culled,
          stats->unsupported_invalid, stats->decode_failures, stats->vertices,
          stats->faces, stats->filled_faces, stats->filled_pixels, stats->lines,
          stats->line_pixels, stats->cockpit_pixels);
}

static bool source_object_has_drawable_shape(const NativeSourceObject *object) {
  return object && object->shape != 0 && object->shape != kShapeNull &&
         (object->sflags[0] & (kAsfPartObj | kAsfTextObj)) == 0 &&
         (object->sflags[3] & kAsfInvisible4) == 0;
}

static bool source_object_has_native_shape(const NativeSourceObject *object) {
  return source_object_has_drawable_shape(object) &&
         (object->sflags[0] & kAsfShadowShape) == 0;
}

static bool source_object_has_shadow_shape(const NativeSourceObject *object) {
  return source_object_has_drawable_shape(object) &&
         (object->sflags[0] & kAsfShadowMask) != 0;
}

static uint16_t effective_colour_pointer(const NativeSourceObject *object) {
  const uint8_t flags = object->sflags[0];
  if ((flags & kAsfTextObj) != 0)
    return 0;
  if ((flags & 0x02u) != 0 && (flags & kAsfScaledSprite) == 0) {
    return (flags & 0x01u) != 0 ? kRomColourRed : kRomColourWhite;
  }
  return (flags & 0x01u) != 0 ? kRomColourSpecial : object->colour_pointer;
}

static void source_snapshot_insert_drawable(NativeSourceFrameSnapshot *snapshot,
                                            uint8_t object_index) {
  unsigned position = snapshot->draw_count;
  const NativeSourceObject *candidate = &snapshot->objects[object_index];
  for (unsigned i = 0; i < snapshot->draw_count; i++) {
    const NativeSourceObject *existing =
        &snapshot->objects[snapshot->draw_order[i]];
    const uint16_t difference =
        (uint16_t)(existing->sort_depth - candidate->sort_depth);
    if ((difference & 0x8000u) != 0) {
      position = i;
      break;
    }
  }
  if (position < snapshot->draw_count) {
    memmove(&snapshot->draw_order[position + 1],
            &snapshot->draw_order[position], snapshot->draw_count - position);
  }
  snapshot->draw_order[position] = object_index;
  snapshot->draw_count++;
}

static void latch_source_object(NativeSourceFrameSnapshot *snapshot,
                                uint16_t pointer, unsigned slot) {
  NativeSourceObject object;
  (void)slot;
  memset(&object, 0, sizeof(object));
  object.pointer = pointer;
  object.shape = ram_word(pointer + kObjShape);
  object.flags = ram_byte(pointer + kObjFlags);
  object.type = ram_byte(pointer + kObjType);
  object.count = ram_byte(pointer + kObjCounter);
  object.world_x = ram_i16(pointer + kObjWorldX);
  object.world_y = ram_i16(pointer + kObjWorldY);
  object.world_z = ram_i16(pointer + kObjWorldZ);
  object.pitch = ram_byte(pointer + kObjRotX);
  object.yaw = ram_byte(pointer + kObjRotY);
  object.roll = ram_byte(pointer + kObjRotZ);
  object.sflags[0] = ram_byte(pointer + kObjSFlags);
  object.sflags[1] = ram_byte(pointer + kObjSFlags2);
  object.sflags[2] = ram_byte(pointer + kObjSFlags3);
  object.sflags[3] = ram_byte(pointer + kObjSFlags4);
  object.object_depth_offset = ram_byte(pointer + kObjAuxDepthOffset);
  object.colour_frame = ram_byte(pointer + kObjAuxColourFrame);
  object.animation_frame = ram_byte(pointer + kObjAuxAnimationFrame);
  object.colour_pointer = ram_word(pointer + kObjAuxColourTable);
  object.texture_scroll_x = ram_i8(pointer + kObjAuxTextureScrollX);
  object.texture_scroll_y = ram_i8(pointer + kObjAuxTextureScrollY);
  object.explosion_count =
      (object.flags & kAfExplosion) != 0 ? object.count : 0;
  snapshot->objects[snapshot->active_count++] = object;
}

static bool classify_and_sort_source_object(NativeSourceFrameSnapshot *snapshot,
                                            const Cart *cart,
                                            unsigned object_index) {
  NativeSourceObject *object = &snapshot->objects[object_index];
  int16_t sort_z = 0;
  int16_t z_max = 0;
  const int16_t relative_x = subtract16(object->world_x, snapshot->view_x);
  const int16_t relative_y = subtract16(object->world_y, snapshot->view_y);
  const int16_t relative_z = subtract16(object->world_z, snapshot->view_z);

  if ((object->sflags[3] & kAsfInvisible4) != 0) {
    snapshot->unsupported_invisible++;
    return false;
  }
  if ((object->sflags[0] & kAsfPartObj) != 0) {
    snapshot->unsupported_particle++;
    return false;
  }
  if ((object->sflags[0] & kAsfTextObj) != 0) {
    snapshot->unsupported_text++;
    return false;
  }
  if (!object->shape || object->shape == kShapeNull) {
    return false;
  }
  if (!read_shape_metadata(cart, object->shape, &sort_z, &z_max)) {
    snapshot->unsupported_invalid++;
    return false;
  }

  object->camera_x = transform_q15_component(snapshot->view_matrix, relative_x,
                                             relative_y, relative_z, 0);
  object->camera_y = transform_q15_component(snapshot->view_matrix, relative_x,
                                             relative_y, relative_z, 1);
  object->camera_z = transform_q15_component(snapshot->view_matrix, relative_x,
                                             relative_y, relative_z, 2);
  object->sort_depth = add16(object->camera_z, sort_z);
  if ((object->type & kAtGround) != 0)
    object->sort_depth = add16(object->sort_depth, 15000);
  if (add16(object->camera_z, z_max) < 0) {
    snapshot->unsupported_culled++;
    return false;
  }

  object->colour_pointer = effective_colour_pointer(object);
  source_snapshot_insert_drawable(snapshot, (uint8_t)object_index);
  return true;
}

static bool latch_source_pool_object(NativeSourceFrameSnapshot *snapshot,
                                     const Cart *cart, uint16_t pointer,
                                     unsigned slot) {
  if (snapshot->active_count >= kObjPoolCount)
    return false;
  latch_source_object(snapshot, pointer, slot);
  classify_and_sort_source_object(snapshot, cart, snapshot->active_count - 1u);
  return true;
}

static bool latch_allst_objects(NativeSourceFrameSnapshot *snapshot,
                                const Cart *cart) {
  bool seen[kObjPoolCount];
  uint16_t pointer = ram_word(kRamAllst);
  memset(seen, 0, sizeof(seen));

  while (pointer != 0) {
    unsigned slot = 0;
    uint16_t next = 0;
    if (!object_pointer_index(pointer, &slot) || seen[slot])
      return false;
    seen[slot] = true;
    next = ram_word(pointer + kObjNext);
    if (!latch_source_pool_object(snapshot, cart, pointer, slot))
      return false;
    pointer = next;
  }
  return true;
}

void StarFoxEnhancedLatchSourceFrame(void) {
  NativeSourceFrameSnapshot snapshot;
  Cart *cart = g_snes ? g_snes->cart : NULL;
  memset(&snapshot, 0, sizeof(snapshot));

  if (!g_config.enhanced_renderer || !native_shape_overlay_enabled() || !cart ||
      !cart->rom || !cart->romSize)
    goto done;

  extern int snes_frame_counter;
  snapshot.frame = snes_frame_counter;
  snapshot.game_frame = (uint8_t)(ram_byte(kRamGameFrame) & 0x7f);
  snapshot.view_x = ram_i16(kRamViewPosX);
  snapshot.view_y = ram_i16(kRamViewPosY);
  snapshot.view_z = ram_i16(kRamViewPosZ);
  for (unsigned i = 0; i < 9; i++)
    snapshot.view_matrix[i] = ram_i16(kRamWmat11W + i * 2u);
  {
    int16_t source_vanish_x = ram_i16(kRamVanishX);
    int16_t source_vanish_y = ram_i16(kRamVanishY);
    const uint16_t gsu_depth_colours = gsu_word(kGsuDepthColours);
    const uint16_t gsu_depth_thresholds = gsu_word(kGsuDepthThresholds);
    /* GSU scratch may be zero outside a source task; WRAM mirrors survive. */
    if (source_vanish_x == 0)
      source_vanish_x = kSourceVanishDefaultX;
    if (source_vanish_y == 0)
      source_vanish_y = kSourceVanishDefaultY;
    snapshot.vanish_x = add16(source_vanish_x, kSuperFxHorizontalInset);
    snapshot.vanish_y = add16(source_vanish_y, kSuperFxVerticalInset);
    snapshot.player_fly_mode = (uint8_t)gsu_word(kGsuPlayerFlyMode);
    snapshot.shadow_height = (int16_t)gsu_word(kGsuShadowHeight);
    snapshot.hud_rotation = ram_word(kRamHudRotation);
    snapshot.hud_colour = gsu_byte(kGsuHudColour);
    snapshot.hud_damage_flags = gsu_byte(kGsuHudDamageFlags);
    snapshot.depth_colours =
        gsu_depth_colours != 0 ? gsu_depth_colours : ram_word(kRamDepthTabPtr);
    snapshot.depth_thresholds = gsu_depth_thresholds != 0
                                    ? gsu_depth_thresholds
                                    : kRomDepthThresholdDefault;
  }

  if (!latch_allst_objects(&snapshot, cart)) {
    memset(&snapshot, 0, sizeof(snapshot));
    goto done;
  }
  snapshot.valid = snapshot.active_count != 0;

done:
  g_source_snapshot = snapshot;
}

static bool source_snapshot_current(void) {
  if (!g_source_snapshot.valid)
    return false;
  extern int snes_frame_counter;
  const int age = snes_frame_counter - g_source_snapshot.frame;
  return age >= 0 && age <= 1;
}

static bool source_snapshot_is_gameplay_training_world_frame(void) {
  if (!source_snapshot_current())
    return false;
  if (g_source_snapshot.active_count < kNativeWorldMinActiveObjects)
    return false;
  if (g_source_snapshot.unsupported_text != 0)
    return false;
  return true;
}

static bool native_world_replacement_ready(const NativeRendererStats *stats) {
  if (!source_snapshot_is_gameplay_training_world_frame() || !stats)
    return false;
  return stats->drawn >= kNativeWorldMinDrawnShapes &&
         stats->filled_pixels >= kNativeWorldMinVisiblePixels;
}

static void log_native_world_gate_transition(const NativeRendererStats *stats,
                                             int raw_ready, int suppress,
                                             int native_ppu_done) {
  static int initialized;
  static int last_raw_ready;
  static int last_suppress;
  static int last_native_ppu_done;
  if (!native_world_gate_diagnostics_enabled() || !stats)
    return;
  if (initialized && last_raw_ready == raw_ready &&
      last_suppress == suppress && last_native_ppu_done == native_ppu_done) {
    return;
  }
  initialized = 1;
  last_raw_ready = raw_ready;
  last_suppress = suppress;
  last_native_ppu_done = native_ppu_done;
  extern int snes_frame_counter;
  fprintf(stderr,
          "[starfox-native-world-gate] frame=%d snap_frame=%d raw_ready=%d "
          "suppress=%d native_ppu_done=%d source=%u/%u drawn=%u pixels=%u "
          "unsupported inv=%u shadow=%u particle=%u scaled=%u text=%u "
          "culled=%u invalid=%u decode=%u\n",
          snes_frame_counter, g_source_snapshot.frame, raw_ready, suppress,
          native_ppu_done, g_source_snapshot.draw_count,
          g_source_snapshot.active_count, stats->drawn, stats->filled_pixels,
          stats->unsupported_invisible, stats->unsupported_shadow,
          stats->unsupported_particle, stats->unsupported_scaled,
          stats->unsupported_text, stats->unsupported_culled,
          stats->unsupported_invalid, stats->decode_failures);
}

static unsigned display_frame(uint8_t object_frame) {
  return (object_frame & 0x80u) != 0 ? (unsigned)(object_frame & 0x7fu)
                                     : (unsigned)g_source_snapshot.game_frame;
}

static void fill_native_shape_pose(StarFoxEnhancedNativeShapePose *pose,
                                   const NativeSourceObject *object,
                                   uint16_t ws_extra, int shadow) {
  const int true_colour_shadow =
      (object->sflags[0] & kAsfShadowShape) != 0;
  memset(pose, 0, sizeof(*pose));
  if (shadow && !true_colour_shadow) {
    const int16_t relative_x =
        subtract16(object->world_x, g_source_snapshot.view_x);
    const int16_t relative_y =
        subtract16(g_source_snapshot.shadow_height, g_source_snapshot.view_y);
    const int16_t relative_z =
        subtract16(object->world_z, g_source_snapshot.view_z);
    pose->x = transform_q15_component(g_source_snapshot.view_matrix,
                                      relative_x, relative_y, relative_z, 0);
    pose->y = transform_q15_component(g_source_snapshot.view_matrix,
                                      relative_x, relative_y, relative_z, 1);
    pose->z = transform_q15_component(g_source_snapshot.view_matrix,
                                      relative_x, relative_y, relative_z, 2);
  } else {
    pose->x = object->camera_x;
    pose->y = object->camera_y;
    pose->z = object->camera_z;
  }
  pose->pitch = (uint16_t)object->pitch << 8;
  pose->yaw = (uint16_t)object->yaw << 8;
  pose->roll = (uint16_t)object->roll << 8;
  pose->vanish_x = g_source_snapshot.vanish_x;
  pose->vanish_y = g_source_snapshot.vanish_y;
  pose->colour_pointer = object->colour_pointer;
  pose->depth_colours = g_source_snapshot.depth_colours;
  pose->depth_thresholds = g_source_snapshot.depth_thresholds;
  pose->object_depth_offset = object->object_depth_offset;
  pose->explosion_progress = object->explosion_count;
  pose->use_source_view_matrix = 1;
  pose->texture_scroll_x = object->texture_scroll_x;
  pose->texture_scroll_y = object->texture_scroll_y;
  if ((object->sflags[0] & kAsfScaledSprite) != 0) {
    pose->simple_scaled_sprite = 1;
    pose->simple_sprite_colour = object->object_depth_offset;
  }
  pose->animation_frame = display_frame(object->animation_frame);
  pose->colour_frame = display_frame(object->colour_frame);
  memcpy(pose->source_view_matrix, g_source_snapshot.view_matrix,
         sizeof(pose->source_view_matrix));
  pose->widescreen_extra = ws_extra;
  if (shadow) {
    pose->use_shadow_shape = 1;
    pose->flatten_shadow_matrix = 1;
    if (!true_colour_shadow) {
      pose->force_colour = 1;
      pose->forced_colour = kShadowForcedColour;
    }
  }
}

static unsigned draw_native_shape_object(uint8_t *pixels, size_t pitch,
                                         int width, int height,
                                         const Cart *cart, uint16_t ws_extra,
                                         unsigned draw_index,
                                         const NativeSourceObject *object,
                                         int shadow,
                                         NativeRendererStats *renderer_stats) {
  StarFoxEnhancedNativeShapePose pose;
  StarFoxEnhancedNativeShapeStats stats;
  if (renderer_stats)
    renderer_stats->candidates++;
  fill_native_shape_pose(&pose, object, ws_extra, shadow);
  const int shape_visible = StarFoxEnhancedDrawNativeShape(
      pixels, pitch, width, height, cart->rom, cart->romSize, object->shape,
      &pose, &stats);
  log_native_shape_diagnostic(draw_index, object, &pose, &stats, shape_visible);
  if (renderer_stats) {
    renderer_stats->filled_pixels += stats.visible_pixels;
    renderer_stats->decode_failures += stats.decode_failures;
    renderer_stats->vertices += stats.decoded_vertices;
    renderer_stats->faces += stats.decoded_faces;
  }
  if (!shape_visible)
    return 0;
  if (renderer_stats)
    renderer_stats->drawn++;
  return 1;
}

static unsigned
draw_source_snapshot_shapes(uint8_t *pixels, size_t pitch, int width,
                            int height, uint16_t ws_extra,
                            NativeRendererStats *renderer_stats) {
  Cart *cart = g_snes ? g_snes->cart : NULL;
  if (!source_snapshot_current() || !cart || !cart->rom || !cart->romSize ||
      !pixels)
    return 0;

  unsigned drawn = 0;
  if (renderer_stats) {
    renderer_stats->unsupported_invisible =
        g_source_snapshot.unsupported_invisible;
    renderer_stats->unsupported_shadow = g_source_snapshot.unsupported_shadow;
    renderer_stats->unsupported_particle =
        g_source_snapshot.unsupported_particle;
    renderer_stats->unsupported_scaled = g_source_snapshot.unsupported_scaled;
    renderer_stats->unsupported_text = g_source_snapshot.unsupported_text;
    renderer_stats->unsupported_culled = g_source_snapshot.unsupported_culled;
    renderer_stats->unsupported_invalid = g_source_snapshot.unsupported_invalid;
  }

  /* Shadow pass */
  if ((g_source_snapshot.player_fly_mode & kPfmShadows) != 0) {
    for (unsigned i = 0; i < g_source_snapshot.draw_count; i++) {
      const NativeSourceObject *object =
          &g_source_snapshot.objects[g_source_snapshot.draw_order[i]];
      if (source_object_has_shadow_shape(object)) {
        drawn += draw_native_shape_object(pixels, pitch, width, height, cart,
                                          ws_extra, i, object, 1,
                                          renderer_stats);
      }
    }
  }

  /* Normal object pass */
  for (unsigned i = 0; i < g_source_snapshot.draw_count; i++) {
    const NativeSourceObject *object =
        &g_source_snapshot.objects[g_source_snapshot.draw_order[i]];
    if (renderer_stats)
      renderer_stats->entries++;
    if (source_object_has_native_shape(object)) {
      drawn += draw_native_shape_object(pixels, pitch, width, height, cart,
                                        ws_extra, i, object, 0,
                                        renderer_stats);
    }
  }
  if ((g_source_snapshot.hud_rotation & 0x8000u) != 0) {
    const uint8_t override =
        g_source_snapshot.hud_colour >= 128u ? g_source_snapshot.hud_colour
                                            : 0u;
    const unsigned cockpit_pixels = StarFoxEnhancedDrawCockpitHud(
        pixels, pitch, width, height, cart->rom, cart->romSize,
        (uint8_t)g_source_snapshot.hud_rotation,
        g_source_snapshot.hud_colour, g_source_snapshot.hud_damage_flags,
        (int)ws_extra + kSuperFxHorizontalInset, kSuperFxVerticalInset,
        override);
    drawn += cockpit_pixels != 0 ? 1u : 0u;
    if (renderer_stats)
      renderer_stats->cockpit_pixels += cockpit_pixels;
  }
  return drawn;
}

static void clear_frame(uint8_t *pixels, size_t pitch, int width, int height) {
  if (!pixels || width <= 0 || height <= 0)
    return;
  for (int y = 0; y < height; y++)
    memset(pixels + (size_t)y * pitch, 0, (size_t)width * 4u);
}

static uint8_t *allocate_bgra_scratch(int width, int height, size_t *pitch) {
  size_t scratch_pitch = 0;
  if (pitch)
    *pitch = 0;
  if (width <= 0 || height <= 0)
    return NULL;
  scratch_pitch = (size_t)width * 4u;
  if (pitch)
    *pitch = scratch_pitch;
  return (uint8_t *)calloc((size_t)height, scratch_pitch);
}

static void composite_bgra_nonzero(uint8_t *dst, size_t dst_pitch,
                                   const uint8_t *src, size_t src_pitch,
                                   int width, int height) {
  if (!dst || !src || width <= 0 || height <= 0 ||
      dst_pitch < (size_t)width * 4u || src_pitch < (size_t)width * 4u)
    return;
  for (int y = 0; y < height; y++) {
    uint8_t *dst_row = dst + (size_t)y * dst_pitch;
    const uint8_t *src_row = src + (size_t)y * src_pitch;
    for (int x = 0; x < width; x++) {
      const uint8_t *s = src_row + (size_t)x * 4u;
      if (s[0] == 0 && s[1] == 0 && s[2] == 0 && s[3] == 0)
        continue;
      memcpy(dst_row + (size_t)x * 4u, s, 4u);
    }
  }
}

static void copy_stock_center(const RtlEnhancedRendererFrame *frame) {
  if (!frame || !frame->pixels || !g_ppu || !g_ppu->renderBuffer ||
      !g_ppu->renderPitch || frame->width <= 0 || frame->height <= 0)
    return;
  const int copy_width =
      frame->width - (int)frame->widescreen_extra >= 256 ? 256 : frame->width;
  const int dst_left = frame->width >= 256 + 2 * (int)frame->widescreen_extra
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
      !frame->widescreen_extra)
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
      const uint8_t key = (uint8_t)(((r & 0xc0u) >> 2) | ((g & 0xc0u) >> 4) |
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

static void dump_bgra_bmp(const char *path, const uint8_t *pixels, size_t pitch,
                          int width, int height) {
  if (!path || !*path || !pixels || pitch < (size_t)width * 4u || width <= 0 ||
      height <= 0)
    return;
  FILE *f = fopen(path, "wb");
  if (!f)
    return;
  const uint32_t image_size = (uint32_t)width * (uint32_t)height * 4u;
  const uint32_t header_size = 14u + 40u;
  const uint32_t file_size = header_size + image_size;
  uint8_t hdr[54] = {'B', 'M'};
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

static void dump_bgra_png(const char *path, const uint8_t *pixels, size_t pitch,
                          int width, int height) {
  if (!path || !*path || !pixels || pitch < (size_t)width * 4u || width <= 0 ||
      height <= 0)
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

RtlEnhancedRenderResult
StarFoxEnhancedRenderFrame(RtlEnhancedRendererFrame *frame) {
  if (!frame)
    return kRtlEnhancedRender_NotHandled;
  if (frame->default_renderer_done)
    return kRtlEnhancedRender_Handled;
  const bool shape_overlay_enabled = native_shape_overlay_enabled();
  NativeRendererStats stats;
  size_t native_world_pitch = 0;
  uint8_t *native_world = NULL;
  unsigned drawn = 0;
  bool native_world_ready = false;
  bool suppress_superfx_world_bg1 = false;
  memset(&stats, 0, sizeof(stats));

  if (shape_overlay_enabled) {
    native_world =
        allocate_bgra_scratch(frame->width, frame->height, &native_world_pitch);
    if (native_world) {
      drawn = draw_source_snapshot_shapes(native_world, native_world_pitch,
                                          frame->width, frame->height,
                                          frame->widescreen_extra, &stats);
      native_world_ready = native_world_replacement_ready(&stats);
      suppress_superfx_world_bg1 = native_world_ready;
      stats.native_world_ready = native_world_ready ? 1u : 0u;
      stats.native_world_suppressed = suppress_superfx_world_bg1 ? 1u : 0u;
    }
  }

  clear_frame(frame->pixels, frame->pitch, frame->width, frame->height);
  StarFoxDrawPpuFrame();
  int native_ppu_done = StarFoxEnhancedDrawNativePpuLayers(
      frame->pixels, frame->pitch, frame->width, frame->height,
      frame->widescreen_extra, suppress_superfx_world_bg1 ? 1 : 0);
  if (!suppress_superfx_world_bg1 && native_ppu_done &&
      native_frame_looks_suspect(frame)) {
    clear_frame(frame->pixels, frame->pitch, frame->width, frame->height);
    native_ppu_done = 0;
    stats.declined_native_ppu++;
  }
  log_native_world_gate_transition(&stats, native_world_ready ? 1 : 0,
                                   suppress_superfx_world_bg1 ? 1 : 0,
                                   native_ppu_done);
  if (!native_ppu_done && !suppress_superfx_world_bg1)
    copy_stock_center(frame);
  if (shape_overlay_enabled) {
    if (suppress_superfx_world_bg1) {
      composite_bgra_nonzero(frame->pixels, frame->pitch, native_world,
                             native_world_pitch, frame->width, frame->height);
    }
    log_renderer_stats(&stats, frame->widescreen_extra, frame->width,
                       frame->height);
  }
  if (!drawn && debug_probe_enabled()) {
    draw_debug_probe(frame->pixels, frame->pitch, frame->width, frame->height,
                     frame->widescreen_extra);
  }
  free(native_world);
  maybe_dump_frame(frame);
  return kRtlEnhancedRender_Handled;
}
