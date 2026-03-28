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

/* Check if a BG layer is a solid-color fill (>80% same quantized color).
 * These layers are flat backgrounds that block the 3D view uselessly. */
static bool is_solid_color_layer(const ExtractedFrame *frame, int check_layer) {
    /* Quantize colors to 4-bit per channel and count the most common */
    int total = 0, max_count = 0;
    uint16_t max_key = 0;

    /* Simple frequency count with small bucket array */
    typedef struct { uint16_t key; int count; } Bucket;
    Bucket buckets[64];
    int nbuckets = 0;

    for (int i = 0; i < frame->bg_tile_count; i++) {
        if (frame->bg_tiles[i].bg_layer != check_layer) continue;
        /* Sample 4 corners per tile for speed */
        const int coords[4][2] = {{0,0},{0,7},{7,0},{7,7}};
        for (int s = 0; s < 4; s++) {
            const uint8_t *px = frame->bg_tiles[i].decoded.pixels[coords[s][0]][coords[s][1]];
            if (px[3] == 0) continue;
            if (px[0] + px[1] + px[2] < 12) continue;
            total++;
            uint16_t key = ((px[0] >> 4) << 8) | ((px[1] >> 4) << 4) | (px[2] >> 4);
            int found = -1;
            for (int b = 0; b < nbuckets; b++) {
                if (buckets[b].key == key) { found = b; break; }
            }
            if (found >= 0) {
                buckets[found].count++;
                if (buckets[found].count > max_count) {
                    max_count = buckets[found].count;
                    max_key = key;
                }
            } else if (nbuckets < 64) {
                buckets[nbuckets].key = key;
                buckets[nbuckets].count = 1;
                if (1 > max_count) { max_count = 1; max_key = key; }
                nbuckets++;
            }
        }
    }
    /* If >80% of sampled pixels are the same color, it's a solid fill */
    return total > 50 && max_count > total * 80 / 100;
}

/* Voxelize background tiles with depth extrusion + brightness + priority */
static void voxelize_bg_tiles(const ExtractedFrame *frame,
                               const VoxelProfile *profile,
                               VoxelMesh *mesh,
                               uint8_t visible_layers)
{
    float scale = profile->pixel_scale;
    float bright_scale = profile->brightness_depth;

    /* Pre-pass: detect solid-color layers to skip */
    bool skip_layer[4] = {false, false, false, false};
    for (int l = 0; l < 4; l++) {
        if (profile->bg_depth[l] <= 0.0f) continue;
        if (is_solid_color_layer(frame, l)) {
            skip_layer[l] = true;
        }
    }

    for (int i = 0; i < frame->bg_tile_count; i++) {
        const ExtractedBgTile *bt = &frame->bg_tiles[i];
        int layer = bt->bg_layer;

        /* Skip layers with zero depth, solid-color fill, or hidden by editor */
        if (profile->bg_depth[layer] <= 0.0f) continue;
        if (skip_layer[layer]) continue;
        if (!(visible_layers & (1 << layer))) continue;

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

                /* Apply per-layer alpha multiplier */
                uint8_t final_alpha = (uint8_t)(px[3] * profile->layer_alpha[layer]);

                /* Extrude upward (Y+) — same color throughout for clean sides */
                for (int d = 0; d < total_depth; d++) {
                    float wy = y_base + d;
                    mesh_push(mesh, wx, wy, wz, px[0], px[1], px[2], final_alpha);
                }
            }
        }
    }
}

/* ---- Union-Find for sprite grouping ---- */
static int uf_find(int *parent, int i) {
    while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
    return i;
}
static void uf_union(int *parent, int *rank, int a, int b) {
    a = uf_find(parent, a); b = uf_find(parent, b);
    if (a == b) return;
    if (rank[a] < rank[b]) { int t = a; a = b; b = t; }
    parent[b] = a;
    if (rank[a] == rank[b]) rank[a]++;
}

static int group_sprites(const ExtractedSprite *sprites, int count, int gap,
                         SpriteGroup *groups, int max_groups) {
    if (count <= 0) return 0;
    if (count > 128) count = 128; /* safety cap */

    int parent[128], rank_arr[128];
    for (int i = 0; i < count; i++) { parent[i] = i; rank_arr[i] = 0; }

    /* Union sprites that overlap or are within gap pixels */
    for (int i = 0; i < count; i++) {
        int ax0 = sprites[i].screen_x;
        int ay0 = sprites[i].screen_y;
        int ax1 = ax0 + sprites[i].width;
        int ay1 = ay0 + sprites[i].height;

        for (int j = i + 1; j < count; j++) {
            int bx0 = sprites[j].screen_x;
            int by0 = sprites[j].screen_y;
            int bx1 = bx0 + sprites[j].width;
            int by1 = by0 + sprites[j].height;

            /* Check overlap with gap tolerance */
            if (ax0 - gap <= bx1 && ax1 + gap >= bx0 &&
                ay0 - gap <= by1 && ay1 + gap >= by0) {
                /* Also check same priority for grouping */
                if (sprites[i].priority == sprites[j].priority) {
                    uf_union(parent, rank_arr, i, j);
                }
            }
        }
    }

    /* Collect groups */
    int ngroups = 0;
    int group_id[128];
    memset(group_id, -1, sizeof(group_id));

    for (int i = 0; i < count; i++) {
        int root = uf_find(parent, i);
        if (group_id[root] < 0) {
            if (ngroups >= max_groups) break;
            group_id[root] = ngroups;
            SpriteGroup *g = &groups[ngroups];
            g->count = 0;
            g->min_x = 9999; g->min_y = 9999;
            g->max_x = -9999; g->max_y = -9999;
            ngroups++;
        }
        int gi = group_id[root];
        SpriteGroup *g = &groups[gi];
        if (g->count < 64) {
            g->indices[g->count++] = i;
        }
        int x0 = sprites[i].screen_x;
        int y0 = sprites[i].screen_y;
        int x1 = x0 + sprites[i].width;
        int y1 = y0 + sprites[i].height;
        if (x0 < g->min_x) g->min_x = x0;
        if (y0 < g->min_y) g->min_y = y0;
        if (x1 > g->max_x) g->max_x = x1;
        if (y1 > g->max_y) g->max_y = y1;
    }

    return ngroups;
}

/* Voxelize sprites with uniform depth (no brightness variation — keeps sprites solid) */
static void voxelize_sprites(const ExtractedFrame *frame,
                              const VoxelProfile *profile,
                              VoxelMesh *mesh,
                              uint8_t visible_layers)
{
    if (!(visible_layers & 0x10)) return; /* bit 4 = sprites */
    float scale = profile->pixel_scale;
    float y_base = profile->sprite_z;
    float depth = profile->sprite_depth;
    int total_depth = (int)(depth + 0.5f);
    if (total_depth < 1) total_depth = 1;

    float bright_scale = profile->brightness_depth;

    if (profile->sprite_grouping) {
        /* ---- Grouped sprite rendering ---- */
        SpriteGroup groups[64];
        int ngroups = group_sprites(frame->sprites, frame->sprite_count,
                                    profile->group_gap, groups, 64);

        for (int gi = 0; gi < ngroups; gi++) {
            SpriteGroup *grp = &groups[gi];

            /* Find max priority in group for shared Y base */
            int max_prio = 0;
            for (int si = 0; si < grp->count; si++) {
                int idx = grp->indices[si];
                if (frame->sprites[idx].priority > max_prio)
                    max_prio = frame->sprites[idx].priority;
            }
            float y_off = y_base + max_prio * 1.5f;

            /* Voxelize all sprites in group with shared Y base */
            for (int si = 0; si < grp->count; si++) {
                int idx = grp->indices[si];
                const ExtractedSprite *sp = &frame->sprites[idx];

                for (int row = 0; row < sp->height && row < 64; row++) {
                    for (int col = 0; col < sp->width && col < 64; col++) {
                        const uint8_t *px = sp->pixels[row][col];
                        if (px[3] == 0) continue;

                        int sx = sp->screen_x + col;
                        int sy = sp->screen_y + row;
                        if (sx < 0 || sx >= 256 || sy < 0 || sy >= 224) continue;

                        float wx = sx * scale;
                        float wz = sy * scale;

                        float bright = pixel_brightness(px[0], px[1], px[2]);
                        int extra = (int)(bright * bright_scale * total_depth * 0.5f + 0.5f);
                        int sprite_h = total_depth + extra;

                        uint8_t final_alpha = (uint8_t)(px[3] * profile->sprite_alpha);

                        for (int d = 0; d < sprite_h; d++) {
                            float wy = y_off + d;
                            mesh_push(mesh, wx, wy, wz, px[0], px[1], px[2], final_alpha);
                        }
                    }
                }
            }
        }
    } else {
        /* ---- Non-grouped sprite rendering (original code) ---- */
        for (int i = 0; i < frame->sprite_count; i++) {
            const ExtractedSprite *sp = &frame->sprites[i];

            /* All sprites at same base height -- priority gives slight lift */
            float y_off = y_base + sp->priority * 1.5f;

            for (int row = 0; row < sp->height && row < 64; row++) {
                for (int col = 0; col < sp->width && col < 64; col++) {
                    const uint8_t *px = sp->pixels[row][col];
                    if (px[3] == 0) continue;

                    int sx = sp->screen_x + col;
                    int sy = sp->screen_y + row;
                    if (sx < 0 || sx >= 256 || sy < 0 || sy >= 224) continue;

                    float wx = sx * scale;
                    float wz = sy * scale;

                    /* Apply sprite alpha multiplier */
                    uint8_t final_alpha = (uint8_t)(px[3] * profile->sprite_alpha);

                    /* Brightness heightmap on sprites too -- faces/highlights pop out */
                    float bright = pixel_brightness(px[0], px[1], px[2]);
                    int extra = (int)(bright * bright_scale * total_depth * 0.5f + 0.5f);
                    int sprite_h = total_depth + extra;

                    for (int d = 0; d < sprite_h; d++) {
                        float wy = y_off + d;
                        mesh_push(mesh, wx, wy, wz, px[0], px[1], px[2], final_alpha);
                    }
                }
            }
        }
    }
}

void voxelize_frame(const ExtractedFrame *frame, const VoxelProfile *profile,
                    VoxelMesh *mesh, uint8_t visible_layers)
{
    voxel_mesh_clear(mesh);
    /* Sprites first — they're the most important visual elements
     * (characters, items, projectiles) and must not be cut by voxel cap */
    voxelize_sprites(frame, profile, mesh, visible_layers);
    voxelize_bg_tiles(frame, profile, mesh, visible_layers);
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
    p.bg_depth[0] = 6.0f;  /* BG0: solid base — tall enough for real depth */
    p.bg_depth[1] = 3.0f;  /* BG1: secondary layer — moderate */
    p.bg_depth[2] = 1.0f;  /* BG2: HUD/text — thin */
    p.bg_depth[3] = 0.0f;

    p.sprite_z = 5.0f;     /* sprites sit above the base floor */
    p.sprite_depth = 6.0f;  /* sprites are tall — prominent in the scene */
    p.pixel_scale = 1.0f;

    /* Brightness heightmap: bright pixels pop out, dark pixels stay low.
     * Creates an embossed relief effect — highlights literally rise above shadows. */
    p.brightness_depth = 1.5f;
    p.bg_skip_layer = -1;

    p.light_dir[0] = 0.3f; p.light_dir[1] = 0.8f; p.light_dir[2] = 0.5f;
    p.ambient = 0.35f;
    p.diffuse = 0.65f;
    p.layer_alpha[0] = 1.0f; p.layer_alpha[1] = 1.0f;
    p.layer_alpha[2] = 1.0f; p.layer_alpha[3] = 1.0f;
    p.sprite_alpha = 1.0f;

    p.sky_type = 0;
    p.sky_top[0] = 40; p.sky_top[1] = 40; p.sky_top[2] = 80;
    p.sky_bot[0] = 10; p.sky_bot[1] = 10; p.sky_bot[2] = 20;

    p.shadows_enabled = false;
    p.shadow_opacity = 0.5f;
    p.shadow_y = 0.0f;
    p.sprite_grouping = false;
    p.group_gap = 1;

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
    p.bg_z[0] = 3.0f;     /* BG0 (BG1): main level geometry — raised */
    p.bg_z[1] = 0.0f;     /* BG1 (BG2): background scenery — flat behind */
    p.bg_z[2] = 12.0f;    /* BG2 (BG3): above scene (title text, status bar) */
    p.bg_z[3] = 0.0f;

    p.bg_depth[0] = 5.0f;  /* BG0: level blocks — chunky */
    p.bg_depth[1] = 2.0f;  /* BG1: background — moderate depth */
    p.bg_depth[2] = 1.0f;  /* BG2: thin (status bar / title text) */
    p.bg_depth[3] = 0.0f;

    p.sprite_z = 5.0f;
    p.sprite_depth = 5.0f;
    p.pixel_scale = 1.0f;
    p.brightness_depth = 1.5f;  /* strong relief — bright pixels pop */
    p.bg_skip_layer = 1;        /* skip BG1 sky-colored pixels */
    p.sky_r = p.sky_g = p.sky_b = 0; /* set at runtime */

    p.light_dir[0] = 0.3f; p.light_dir[1] = 0.8f; p.light_dir[2] = 0.5f;
    p.ambient = 0.35f;
    p.diffuse = 0.65f;
    p.layer_alpha[0] = 1.0f; p.layer_alpha[1] = 1.0f;
    p.layer_alpha[2] = 1.0f; p.layer_alpha[3] = 1.0f;
    p.sprite_alpha = 1.0f;

    p.sky_type = 0;
    p.sky_top[0] = 40; p.sky_top[1] = 40; p.sky_top[2] = 80;
    p.sky_bot[0] = 10; p.sky_bot[1] = 10; p.sky_bot[2] = 20;

    p.shadows_enabled = false;
    p.shadow_opacity = 0.5f;
    p.shadow_y = 0.0f;
    p.sprite_grouping = false;
    p.group_gap = 1;

    return p;
}

VoxelProfile voxel_profile_generic(void) {
    VoxelProfile p;

    p.bg_z[0] = 3.0f;     /* main playfield — raised */
    p.bg_z[1] = 0.0f;     /* background — flat behind */
    p.bg_z[2] = 12.0f;    /* HUD/overlay — floats above gameplay */
    p.bg_z[3] = 12.0f;    /* HUD/overlay — floats above gameplay */

    p.bg_depth[0] = 5.0f; /* main layer — prominent height */
    p.bg_depth[1] = 2.0f; /* backgrounds — moderate */
    p.bg_depth[2] = 1.0f; /* HUD — thin */
    p.bg_depth[3] = 1.0f; /* HUD — thin */

    p.sprite_z = 5.0f;
    p.sprite_depth = 5.0f;
    p.pixel_scale = 1.0f;
    p.brightness_depth = 1.5f; /* strong relief effect */
    p.bg_skip_layer = -1;

    p.light_dir[0] = 0.3f; p.light_dir[1] = 0.8f; p.light_dir[2] = 0.5f;
    p.ambient = 0.35f;
    p.diffuse = 0.65f;
    p.layer_alpha[0] = 1.0f; p.layer_alpha[1] = 1.0f;
    p.layer_alpha[2] = 1.0f; p.layer_alpha[3] = 1.0f;
    p.sprite_alpha = 1.0f;

    p.sky_type = 0;
    p.sky_top[0] = 40; p.sky_top[1] = 40; p.sky_top[2] = 80;
    p.sky_bot[0] = 10; p.sky_bot[1] = 10; p.sky_bot[2] = 20;

    p.shadows_enabled = false;
    p.shadow_opacity = 0.5f;
    p.shadow_y = 0.0f;
    p.sprite_grouping = false;
    p.group_gap = 1;

    return p;
}
