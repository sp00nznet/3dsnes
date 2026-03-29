/*
 * voxelizer.h — Convert 2D tile data into 3D voxel meshes.
 *
 * Each non-transparent pixel becomes a colored cube (voxel).
 * Tiles are placed at different Z depths based on their layer and priority.
 */

#ifndef VOXELIZER_H
#define VOXELIZER_H

#include <stdint.h>
#include <stdbool.h>
#include "3dsnes/ppu_extract.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Voxel size in world units (1 pixel = 1.0 unit) */
#define VOXEL_SIZE 1.0f

/* Default depth (Z extrusion) for a single voxel layer */
#define VOXEL_DEPTH 1.0f

/* Maximum voxel instances per frame (256*224 = 57344 per layer, 4 layers + sprites) */
#define MAX_VOXELS (256 * 224 * 6)

/* A single voxel instance for GPU instanced rendering */
typedef struct {
    float x, y, z;       /* world position */
    uint8_t r, g, b, a;  /* color */
} VoxelInstance;

/* Layer depth configuration — how high each layer sits in 3D space */
typedef struct {
    float bg_z[4];          /* Y base height for each BG layer */
    float sprite_z;         /* Y base height for sprites */
    float bg_depth[4];      /* extrusion depth per BG layer */
    float sprite_depth;     /* extrusion depth for sprites */
    float pixel_scale;      /* scale factor per pixel (1.0 = 1 unit per pixel) */
    float brightness_depth; /* extra depth from pixel brightness (0=off, 1.0=full) */
    int bg_skip_layer;      /* layer index whose "sky color" pixels are skipped (-1=none) */
    uint8_t sky_r, sky_g, sky_b; /* sky color to skip (set at runtime from BG1) */
    float light_dir[3];     /* normalized light direction (default: 0.3, 0.8, 0.5) */
    float ambient;          /* ambient factor (default: 0.35) */
    float diffuse;          /* diffuse factor (default: 0.65) */
    float layer_alpha[4];   /* per-BG-layer alpha multiplier (0.0-1.0, default 1.0) */
    float sprite_alpha;     /* sprite alpha multiplier (default 1.0) */
    int      sky_type;          /* 0=solid, 1=gradient */
    uint8_t  sky_top[3];        /* gradient top color RGB */
    uint8_t  sky_bot[3];        /* gradient bottom color RGB */
    bool shadows_enabled;       /* enable ground-plane shadow projection */
    float shadow_opacity;       /* 0.0-1.0 how dark shadows get */
    float shadow_y;             /* ground plane Y for shadows */
    bool sprite_grouping;       /* enable sprite grouping (default false) */
    int group_gap;              /* max pixel gap for grouping (default 1) */
} VoxelProfile;

/* Sprite group -- adjacent sprites merged into one 3D object */
typedef struct {
    int indices[64];    /* indices into ExtractedFrame.sprites[] */
    int count;
    int min_x, min_y, max_x, max_y; /* bounding box */
} SpriteGroup;

/* Result of voxelizing a frame */
typedef struct {
    VoxelInstance *instances;  /* dynamically allocated array */
    int count;                 /* number of voxel instances */
    int capacity;              /* allocated capacity */
} VoxelMesh;

/* Initialize a voxel mesh (allocates memory) */
void voxel_mesh_init(VoxelMesh *mesh, int initial_capacity);

/* Free a voxel mesh */
void voxel_mesh_free(VoxelMesh *mesh);

/* Clear the mesh for reuse (keeps allocation) */
void voxel_mesh_clear(VoxelMesh *mesh);

/*
 * Convert an extracted frame into voxel instances.
 * Uses the profile to determine Z placement of layers.
 * visible_layers: bitmask — bits 0-3 = BG layers, bit 4 = sprites. 0x1F = all.
 */
void voxelize_frame(const ExtractedFrame *frame, const VoxelProfile *profile,
                    VoxelMesh *mesh, uint8_t visible_layers);

/* Per-tile override (set by Lua scripts, consumed by voxelizer) */
typedef struct TileOverride {
    float offset_x, offset_y, offset_z;
    float alpha_mul;    /* 1.0 = no change */
    bool hidden;
    bool dirty;         /* any field was modified */
} TileOverride;

/* Per-sprite override */
typedef struct SpriteOverride {
    float offset_x, offset_y, offset_z;
    float alpha_mul;
    bool hidden;
    bool dirty;
} SpriteOverride;

/*
 * Voxelize with Lua script overrides applied.
 * tile_overrides/sprite_overrides may be NULL if no script is active.
 */
void voxelize_frame_ex(const ExtractedFrame *frame, const VoxelProfile *profile,
                       VoxelMesh *mesh, uint8_t visible_layers,
                       const TileOverride *tile_overrides,
                       const SpriteOverride *sprite_overrides);

/* Get the default profile for Zelda: A Link to the Past */
VoxelProfile voxel_profile_zelda_alttp(void);

/* Get the default profile for Super Mario World */
VoxelProfile voxel_profile_smw(void);

/* Get a generic Mode 1 profile (reasonable defaults for any game) */
VoxelProfile voxel_profile_generic(void);

#ifdef __cplusplus
}
#endif

#endif /* VOXELIZER_H */
