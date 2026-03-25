/*
 * soft_renderer.h — Software 3D voxel renderer.
 *
 * Replaces the OpenGL instanced renderer with a CPU-based rasterizer.
 * Projects voxel cubes into screen space, rasterizes visible faces with
 * simple directional lighting, and writes to an RGBA pixel buffer.
 */

#ifndef SOFT_RENDERER_H
#define SOFT_RENDERER_H

#include <stdint.h>
#include <stdbool.h>
#include "3dsnes/voxelizer.h"
#include "3dsnes/camera.h"

typedef struct {
    uint8_t *color_buf;   /* RGBA output buffer (width * height * 4 bytes) */
    float   *depth_buf;   /* Z-buffer (width * height floats, 1.0 = far) */
    int      width;
    int      height;
} SoftRenderer;

/* Initialize with output dimensions. Returns false on allocation failure. */
bool soft_renderer_init(SoftRenderer *r, int width, int height);

/* Free all internal buffers. */
void soft_renderer_shutdown(SoftRenderer *r);

/* Resize the output buffers. */
void soft_renderer_resize(SoftRenderer *r, int width, int height);

/*
 * Render a frame — clears buffers, projects and rasterizes each voxel,
 * writes RGBA pixels to the internal color buffer.
 */
void soft_renderer_draw(SoftRenderer *r, const Camera *cam,
                        const VoxelInstance *voxels, int count);

/* Get the rendered pixel buffer (RGBA, top-down, width*height*4 bytes). */
const uint8_t *soft_renderer_pixels(const SoftRenderer *r);

#endif /* SOFT_RENDERER_H */
