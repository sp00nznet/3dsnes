#ifndef FXAA_H
#define FXAA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Apply FXAA anti-aliasing to an RGBA image buffer.
 * Input and output must be different buffers of size width*height*4 bytes.
 * Pixel format: bytes [0]=A, [1]=B, [2]=G, [3]=R (ABGR little-endian RGBA8888).
 */
void fxaa_apply(const uint8_t *input, uint8_t *output, int width, int height);

#ifdef __cplusplus
}
#endif

#endif
