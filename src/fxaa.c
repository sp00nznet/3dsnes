/*
 * fxaa.c — Simplified FXAA 3.11 (Timothy Lottes) adapted to C.
 *
 * Operates on an RGBA8888 buffer in ABGR byte order (little-endian):
 *   byte[0] = A, byte[1] = B, byte[2] = G, byte[3] = R
 */

#include "3dsnes/fxaa.h"
#include <string.h>
#include <math.h>

/* Compute perceptual luma at pixel (x,y), clamping to image bounds. */
static inline float luma_at(const uint8_t *buf, int x, int y, int w, int h)
{
    if (x < 0) x = 0; if (x >= w) x = w - 1;
    if (y < 0) y = 0; if (y >= h) y = h - 1;
    int idx = (y * w + x) * 4;
    /* bytes: [0]=A, [1]=B, [2]=G, [3]=R */
    return 0.299f * buf[idx + 3] + 0.587f * buf[idx + 2] + 0.114f * buf[idx + 1];
}

void fxaa_apply(const uint8_t *input, uint8_t *output, int width, int height)
{
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;

            float lumaM = luma_at(input, x,   y,   width, height);
            float lumaN = luma_at(input, x,   y-1, width, height);
            float lumaS = luma_at(input, x,   y+1, width, height);
            float lumaE = luma_at(input, x+1, y,   width, height);
            float lumaW = luma_at(input, x-1, y,   width, height);

            float lumaMin = lumaM;
            if (lumaN < lumaMin) lumaMin = lumaN;
            if (lumaS < lumaMin) lumaMin = lumaS;
            if (lumaE < lumaMin) lumaMin = lumaE;
            if (lumaW < lumaMin) lumaMin = lumaW;

            float lumaMax = lumaM;
            if (lumaN > lumaMax) lumaMax = lumaN;
            if (lumaS > lumaMax) lumaMax = lumaS;
            if (lumaE > lumaMax) lumaMax = lumaE;
            if (lumaW > lumaMax) lumaMax = lumaW;

            float range = lumaMax - lumaMin;

            /* If contrast is low, no AA needed */
            if (range < fmaxf(0.0312f, lumaMax * 0.125f)) {
                memcpy(output + idx, input + idx, 4);
                continue;
            }

            /* Sample diagonal neighbors */
            float lumaNW = luma_at(input, x-1, y-1, width, height);
            float lumaNE = luma_at(input, x+1, y-1, width, height);
            float lumaSW = luma_at(input, x-1, y+1, width, height);
            float lumaSE = luma_at(input, x+1, y+1, width, height);

            /* Determine edge direction (horizontal vs vertical) */
            float edgeH = fabsf(-2.0f * lumaW + lumaNW + lumaSW) +
                          fabsf(-2.0f * lumaM + lumaN  + lumaS) * 2.0f +
                          fabsf(-2.0f * lumaE + lumaNE + lumaSE);
            float edgeV = fabsf(-2.0f * lumaN + lumaNW + lumaNE) +
                          fabsf(-2.0f * lumaM + lumaW  + lumaE) * 2.0f +
                          fabsf(-2.0f * lumaS + lumaSW + lumaSE);

            bool isHorizontal = (edgeH >= edgeV);

            /* Compute blend factor from subpixel contrast */
            float blend = 0.0f;
            if (isHorizontal) {
                float lumaAvg = (lumaN + lumaS) * 0.5f;
                float subpixel = fabsf(lumaAvg - lumaM) / range;
                blend = fminf(subpixel, 0.75f);
            } else {
                float lumaAvg = (lumaE + lumaW) * 0.5f;
                float subpixel = fabsf(lumaAvg - lumaM) / range;
                blend = fminf(subpixel, 0.75f);
            }

            /* Determine blend neighbor (side with higher contrast) */
            int bx = x, by = y;
            if (isHorizontal) {
                by = (fabsf(lumaN - lumaM) > fabsf(lumaS - lumaM)) ? y - 1 : y + 1;
            } else {
                bx = (fabsf(lumaW - lumaM) > fabsf(lumaE - lumaM)) ? x - 1 : x + 1;
            }

            /* Clamp blend coordinates */
            if (bx < 0) bx = 0; if (bx >= width)  bx = width  - 1;
            if (by < 0) by = 0; if (by >= height) by = height - 1;

            int bidx = (by * width + bx) * 4;
            float inv = 1.0f - blend;

            output[idx + 0] = 0xFF; /* alpha */
            output[idx + 1] = (uint8_t)(input[idx + 1] * inv + input[bidx + 1] * blend);
            output[idx + 2] = (uint8_t)(input[idx + 2] * inv + input[bidx + 2] * blend);
            output[idx + 3] = (uint8_t)(input[idx + 3] * inv + input[bidx + 3] * blend);
        }
    }
}
