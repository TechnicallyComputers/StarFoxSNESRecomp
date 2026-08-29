#ifndef STARFOX_ENHANCED_NATIVE_H
#define STARFOX_ENHANCED_NATIVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StarFoxEnhancedNativeShapePose {
  int32_t x;
  int32_t y;
  int32_t z;
  uint16_t pitch;
  uint16_t yaw;
  uint16_t roll;
  int16_t vanish_x;
  int16_t vanish_y;
  uint16_t colour_pointer;
  uint16_t depth_colours;
  uint16_t depth_thresholds;
  uint8_t object_depth_offset;
  uint8_t explosion_progress;
  uint8_t use_source_view_matrix;
  uint8_t use_shadow_shape;
  uint8_t flatten_shadow_matrix;
  uint8_t force_colour;
  uint8_t forced_colour;
  int8_t texture_scroll_x;
  int8_t texture_scroll_y;
  uint32_t animation_frame;
  uint32_t colour_frame;
  int16_t source_view_matrix[9];
  uint16_t widescreen_extra;
} StarFoxEnhancedNativeShapePose;

typedef struct StarFoxEnhancedNativeShapeStats {
  unsigned visible_pixels;
  unsigned decode_failures;
  unsigned decoded_vertices;
  unsigned decoded_faces;
  uint16_t selected_lod;
} StarFoxEnhancedNativeShapeStats;

int StarFoxEnhancedDrawNativePpuLayers(uint8_t *pixels, size_t pitch, int width,
                                       int height, uint16_t widescreen_extra,
                                       int suppress_superfx_world_bg1);
int StarFoxEnhancedDrawNativeShape(uint8_t *pixels, size_t pitch, int width,
                                   int height, const uint8_t *rom,
                                   size_t rom_size, uint16_t shape_address,
                                   const StarFoxEnhancedNativeShapePose *pose,
                                   StarFoxEnhancedNativeShapeStats *stats);

#ifdef __cplusplus
}
#endif

#endif
