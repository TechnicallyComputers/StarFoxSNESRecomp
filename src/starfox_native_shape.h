#ifndef STARFOX_NATIVE_SHAPE_H
#define STARFOX_NATIVE_SHAPE_H

#include <stddef.h>
#include <stdint.h>

typedef struct StarFoxNativeShapePose {
  int32_t x;
  int32_t y;
  int32_t z;
  uint16_t pitch;
  uint16_t yaw;
  uint16_t roll;
  int16_t vanish_x;
  int16_t vanish_y;
  uint32_t animation_frame;
  uint16_t widescreen_extra;
  int protect_center_left;
  int protect_center_right;
} StarFoxNativeShapePose;

typedef struct StarFoxNativeShapeStats {
  unsigned vertices;
  unsigned faces;
  unsigned lines;
} StarFoxNativeShapeStats;

int StarFoxNativeDrawShapeWireframe(const uint8_t *rom, size_t rom_size,
                                    uint16_t shape_address,
                                    const StarFoxNativeShapePose *pose,
                                    uint8_t *pixels, size_t pitch,
                                    int width, int height,
                                    StarFoxNativeShapeStats *stats);

#endif
