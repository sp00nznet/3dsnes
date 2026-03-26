/*
 * voxelizer.c — Convert 2D SNES tile data into 3D voxel meshes.
 */

#include "3dsnes/voxelizer.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

void voxel_mesh_init(VoxelMesh *mesh, int initial_capacity) {
    mesh->instances = malloc(sizeof(VoxelInstance) * initial_capacity);
    mesh->count = 0;
    mesh->capacity = initial_capacity;
}

void voxel_mesh_free(VoxelMesh *mesh) {
    free(mesh->instances);
    mesh->instances = NULL;
    mesh->count = 0;
    mesh->capacity = 0;
}

void voxel_mesh_clear(VoxelMesh *mesh) {
    mesh->count = 0;
}

static void mesh_push(VoxelMesh *mesh, float x, float y, float z,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (mesh->count >= mesh->capacity) {
        /* Skip this voxel rather than realloc — prevents heap churn */
        return;
    }
    VoxelInstance *v = &mesh->instances[mesh->count++];
    v->x = x;
    v->y = y;
    v->z = z;
    v->r = r;
    v->g = g;
    v->b = b;
    v->a = a;
}

/* Compute pixel brightness as 0.0-1.0 */
static float pixel_brightness(uint8_t r, uint8_t g, uint8_t b) {
    /* Perceptual luminance */
    return (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
}

/*
 * Coordinate mapping (diorama style):
 *   World X  = SNES screen X (left to right, 0..255)
 *   World Z  = SNES screen Y (top=0=far, bottom=223=near camera)
 *   World Y  = height / depth extrusion (up from table)
 *
 * This lays the SNES screen flat on a table and extrudes walls/sprites upward.
 */

/* Voxelize background tiles with depth extrusion + brightness + priority */
static void voxelize_bg_tiles(const ExtractedFrame *frame,
                               const VoxelProfile *profile,
                               VoxelMesh *mesh)
{
    float scale = profile->pixel_scale;
    float bright_scale = profile->brightness_depth;

    for (int i = 0; i < frame->bg_tile_count; i++) {
        const ExtractedBgTile *bt = &frame->bg_tiles[i];
        int layer = bt->bg_layer;

        /* Skip layers with zero depth */
        if (profile->bg_depth[layer] <= 0.0f) continue;

        float y_base = profile->bg_z[layer];
        float depth = profile->bg_depth[layer];

        /*
         * Priority=1 tiles get extra height boost (walls/foreground).
         * Priority=0 tiles still get base depth — brightness handles
         * the variation (bright surfaces pop up, dark stay low).
         */
        float priority_bonus = bt->priority ? depth * 0.5f : 0.0f;
        int base_depth = (int)(depth + priority_bonus + 0.5f);
        if (base_depth < 1) base_depth = 1;

        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                const uint8_t *px = bt->decoded.pixels[row][col];
                if (px[3] == 0) continue; /* transparent */
                /* Skip near-black pixels — they're invisible and waste voxel budget */
                if (px[0] + px[1] + px[2] < 12) continue;
                /* Skip sky-colored pixels on the background layer */
                if (layer == profile->bg_skip_layer && profile->sky_r > 0) {
                    int dr = (int)px[0] - profile->sky_r;
                    int dg = (int)px[1] - profile->sky_g;
                    int db = (int)px[2] - profile->sky_b;
                    if (dr*dr + dg*dg + db*db < 900) continue; /* within ~30 of sky color */
                }

                float wx = (bt->screen_x + col) * scale;
                float wz = (bt->screen_y + row) * scale;

                /* Brightness-based extra depth: bright pixels are taller */
                float bright = pixel_brightness(px[0], px[1], px[2]);
                int extra = (int)(bright * bright_scale * base_depth + 0.5f);
                int total_depth = base_depth + extra;

                /* Extrude upward (Y+) — same color throughout for clean sides */
                for (int d = 0; d < total_depth; d++) {
                    float wy = y_base + d;
                    mesh_push(mesh, wx, wy, wz, px[0], px[1], px[2], px[3]);
                }
            }
        }
    }
}

/* Voxelize sprites with uniform depth (no brightness variation — keeps sprites solid) */
static void voxelize_sprites(const ExtractedFrame *frame,
                              const VoxelProfile *profile,
                              VoxelMesh *mesh)
{
    float scale = profile->pixel_scale;
    float y_base = profile->sprite_z;
    float depth = profile->sprite_depth;
    int total_depth = (int)(depth + 0.5f);
    if (total_depth < 1) total_depth = 1;

    for (int i = 0; i < frame->sprite_count; i++) {
        const ExtractedSprite *sp = &frame->sprites[i];

        /* All sprites at same base height — priority gives slight lift */
        float y_off = y_base + sp->priority * 1.0f;

        for (int row = 0; row < sp->height && row < 64; row++) {
            for (int col = 0; col < sp->width && col < 64; col++) {
                const uint8_t *px = sp->pixels[row][col];
                if (px[3] == 0) continue;

                int sx = sp->screen_x + col;
                int sy = sp->screen_y + row;
                if (sx < 0 || sx >= 256 || sy < 0 || sy >= 224) continue;

                float wx = sx * scale;
                float wz = sy * scale;

                /* Extrude upward (Y+) — uniform color for clean solid look */
                for (int d = 0; d < total_depth; d++) {
                    float wy = y_off + d;
                    mesh_push(mesh, wx, wy, wz, px[0], px[1], px[2], px[3]);
                }
            }
        }
    }
}

void voxelize_frame(const ExtractedFrame *frame, const VoxelProfile *profile,
                    VoxelMesh *mesh)
{
    voxel_mesh_clear(mesh);
    voxelize_bg_tiles(frame, profile, mesh);
    voxelize_sprites(frame, profile, mesh);
}

VoxelProfile voxel_profile_zelda_alttp(void) {
    VoxelProfile p;

    /*
     * Zelda: ALTTP uses Mode 1:
     *   BG1 (4bpp) = main playfield (walls, terrain features)
     *   BG2 (4bpp) = ground/floor layer
     *   BG3 (2bpp) = HUD / text overlay
     *
     * Diorama heights (Y axis):
     *   BG2 (ground) at Y=0   — the floor
     *   Sprites      at Y=1   — Link, enemies, items
     *   BG1 (walls)  at Y=2   — raised terrain, walls, pillars
     *   BG3 (HUD)    at Y=12  — floating above everything
     */
    p.bg_z[0] = 0.0f;     /* BG0: playfield starts at ground */
    p.bg_z[1] = 0.0f;     /* BG1: secondary layer */
    p.bg_z[2] = -1.0f;    /* BG2: HUD/text — below scene so it doesn't block 3D */
    p.bg_z[3] = 0.0f;     /* BG3: unused in Mode 1 */

    /*
     * All BG0 tiles get base depth of 8. Brightness adds subtle variation.
     * This gives everything good volume while keeping surfaces smooth.
     */
    p.bg_depth[0] = 4.0f;  /* BG0: solid base depth */
    p.bg_depth[1] = 2.0f;  /* BG1: secondary layer */
    p.bg_depth[2] = 1.0f;  /* BG2: HUD/text — thin */
    p.bg_depth[3] = 0.0f;

    p.sprite_z = 4.0f;     /* sprites sit above the base floor */
    p.sprite_depth = 4.0f;  /* sprites are 4 voxels tall — clean and proportional */
    p.pixel_scale = 1.0f;

    /*
     * Brightness-based depth: subtle variation — keeps surfaces smooth
     * while giving bright areas a gentle lift over dark areas.
     * 0.5 = moderate — bright pixels add up to 50% extra.
     */
    p.brightness_depth = 0.5f;
    p.bg_skip_layer = -1;  /* no sky filtering for Zelda */

    return p;
}

VoxelProfile voxel_profile_smw(void) {
    VoxelProfile p;

    /*
     * Super Mario World uses Mode 1:
     *   BG1 (4bpp) = main level (platforms, blocks, pipes, ground)
     *   BG2 (4bpp) = background scenery (hills, clouds, bushes)
     *   BG3 (2bpp) = status bar at top
     *
     * Diorama heights (Y axis):
     *   BG1 (scenery) at Y=0     — far background, flat
     *   BG0 (level)   at Y=2     — main playfield, extruded
     *   Sprites        at Y=4     — Mario, enemies, items
     *   BG2 (status)  at Y=20    — floating HUD
     */
    p.bg_z[0] = 2.0f;     /* BG0 (BG1): main level geometry */
    p.bg_z[1] = 0.0f;     /* BG1 (BG2): background scenery — flat behind */
    p.bg_z[2] = 7.0f;     /* BG2 (BG3): above scene content (title text, status bar) */
    p.bg_z[3] = 0.0f;

    p.bg_depth[0] = 2.0f;  /* BG0: level blocks */
    p.bg_depth[1] = 1.0f;  /* BG1: background (sky + mountains) — flat */
    p.bg_depth[2] = 1.0f;  /* BG2: thin (status bar / title text) */
    p.bg_depth[3] = 0.0f;

    p.sprite_z = 3.0f;
    p.sprite_depth = 3.0f;
    p.pixel_scale = 1.0f;
    p.brightness_depth = 0.3f;  /* subtle variation */
    p.bg_skip_layer = 1;        /* skip BG1 sky-colored pixels */
    p.sky_r = p.sky_g = p.sky_b = 0; /* set at runtime */

    return p;
}

VoxelProfile voxel_profile_generic(void) {
    VoxelProfile p;

    p.bg_z[0] = 2.0f;     /* main playfield — extruded */
    p.bg_z[1] = 0.0f;     /* background — flat behind */
    p.bg_z[2] = 0.0f;     /* background 2 — flat behind */
    p.bg_z[3] = 0.0f;     /* background 3 — flat behind */

    p.bg_depth[0] = 2.0f; /* main layer gets height */
    p.bg_depth[1] = 1.0f; /* backgrounds are flat */
    p.bg_depth[2] = 1.0f;
    p.bg_depth[3] = 1.0f;

    p.sprite_z = 3.0f;
    p.sprite_depth = 2.0f;
    p.pixel_scale = 1.0f;
    p.brightness_depth = 0.2f; /* less extrusion variation */
    p.bg_skip_layer = -1;

    return p;
}
