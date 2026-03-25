/*
 * renderer.h — OpenGL 3D renderer for voxelized SNES frames.
 *
 * Uses instanced rendering to draw thousands of colored cubes efficiently.
 * Also renders the 2D framebuffer as a fallback/overlay.
 */

#ifndef RENDERER_H
#define RENDERER_H

#include <stdint.h>
#include <stdbool.h>
#include "3dsnes/voxelizer.h"
#include "3dsnes/camera.h"

/* Renderer state */
typedef struct {
    /* Shader programs */
    uint32_t voxel_shader;
    uint32_t quad_shader;

    /* Voxel rendering */
    uint32_t cube_vao;
    uint32_t cube_vbo;         /* unit cube vertex data */
    uint32_t instance_vbo;     /* per-instance data (position + color) */
    int instance_capacity;

    /* 2D overlay (original SNES framebuffer) */
    uint32_t fb_texture;
    uint32_t fb_vao;
    uint32_t fb_vbo;

    /* Viewport */
    int width, height;

    /* Render modes */
    bool show_3d;              /* true = 3D voxels, false = 2D only */
    bool show_overlay;         /* show 2D framebuffer as overlay */
    bool wireframe;            /* wireframe mode */
} Renderer;

/* Initialize the OpenGL renderer */
bool renderer_init(Renderer *r, int width, int height);

/* Cleanup */
void renderer_shutdown(Renderer *r);

/* Resize viewport */
void renderer_resize(Renderer *r, int width, int height);

/* Upload voxel instance data for this frame */
void renderer_upload_voxels(Renderer *r, const VoxelMesh *mesh);

/* Upload the 2D framebuffer texture (RGBX8888, 512x478) */
void renderer_upload_framebuffer(Renderer *r, const uint8_t *pixels,
                                  int width, int height);

/* Render a frame */
void renderer_draw(Renderer *r, const Camera *cam, int voxel_count);

#endif /* RENDERER_H */
