#ifndef STARFOX_ENHANCED_NATIVE_H
#define STARFOX_ENHANCED_NATIVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int StarFoxEnhancedDrawNativePpuLayers(uint8_t *pixels, size_t pitch,
                                       int width, int height,
                                       uint16_t widescreen_extra);

#ifdef __cplusplus
}
#endif

#endif
