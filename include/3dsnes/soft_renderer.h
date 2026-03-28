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
    uint8_t  clear_r, clear_g, clear_b; /* background clear color */
    float    light_dir[3];
    float    ambient;
    float    diffuse;
    float    face_shade[6];    /* pre-computed per-face shading */
    uint8_t *post_buf;          /* secondary buffer for FXAA output */
    bool     fxaa_enabled;
    int      sky_type;          /* 0=solid color, 1=gradient */
    uint8_t  sky_top_r, sky_top_g, sky_top_b;
    uint8_t  sky_bot_r, sky_bot_g, sky_bot_b;
    float   *shadow_buf;      /* shadow accumulation (width * height, 0=lit, 1=full shadow) */
    bool     shadows_enabled;
    float    shadow_opacity;  /* 0.0-1.0 how dark shadows get (default 0.5) */
    float    shadow_y;        /* Y level of ground plane for shadow reception (default 0.0) */
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

/* Set directional lighting parameters and recompute face shading. */
void soft_renderer_set_lighting(SoftRenderer *r, float lx, float ly, float lz,
                                float ambient, float diffuse);

/* Get the rendered pixel buffer (RGBA, top-down, width*height*4 bytes). */
const uint8_t *soft_renderer_pixels(const SoftRenderer *r);

#endif /* SOFT_RENDERER_H */
