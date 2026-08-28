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
  int8_t texture_scroll_x;
  int8_t texture_scroll_y;
  uint32_t animation_frame;
  uint32_t colour_frame;
  uint16_t widescreen_extra;
} StarFoxEnhancedNativeShapePose;

typedef struct StarFoxEnhancedNativeShapeStats {
  unsigned visible_pixels;
  unsigned decode_failures;
} StarFoxEnhancedNativeShapeStats;

int StarFoxEnhancedDrawNativePpuLayers(uint8_t *pixels, size_t pitch,
                                       int width, int height,
                                       uint16_t widescreen_extra);
int StarFoxEnhancedDrawNativeShape(uint8_t *pixels, size_t pitch, int width,
                                   int height, const uint8_t *rom,
                                   size_t rom_size, uint16_t shape_address,
                                   const StarFoxEnhancedNativeShapePose *pose,
                                   StarFoxEnhancedNativeShapeStats *stats);

#ifdef __cplusplus
}
#endif

#endif
