/*
 * ppu_extract.h — Extract tile, sprite, and layer data from LakeSnes PPU state.
 *
 * Reads VRAM, OAM, CGRAM, and BG layer registers to produce structured data
 * that the voxelizer and renderer can consume.
 */

#ifndef PPU_EXTRACT_H
#define PPU_EXTRACT_H

#include <stdint.h>
#include <stdbool.h>

/* Forward-declare LakeSnes types */
typedef struct Ppu Ppu;

/* Maximum entities per frame */
#define MAX_BG_TILES    2048   /* visible tiles across all BG layers */
#define MAX_SPRITES     128    /* OAM supports 128 sprites */

/* A decoded 8x8 tile with palette-resolved RGBA colors */
typedef struct {
    uint8_t pixels[8][8][4];  /* [row][col][RGBA] — 0 alpha = transparent */
} DecodedTile;

/* A positioned tile on a background layer */
typedef struct {
    int screen_x;        /* pixel position on screen (0-255) */
    int screen_y;        /* pixel position on screen (0-223) */
    int bg_layer;        /* which BG layer (0-3) */
    int priority;        /* tile priority bit (0 or 1) */
    int tile_num;        /* VRAM tile number */
    int palette_num;     /* palette index */
    bool hflip, vflip;   /* flip flags */
    DecodedTile decoded; /* fully decoded pixel data */
} ExtractedBgTile;

/* A decoded sprite */
typedef struct {
    int screen_x;        /* X position on screen */
    int screen_y;        /* Y position on screen */
    int width, height;   /* sprite dimensions in pixels */
    int priority;        /* OAM priority (0-3) */
    int palette_num;     /* palette (0-7, offset into OBJ palettes) */
    int tile_num;        /* base tile number */
    bool hflip, vflip;   /* flip flags */
    bool large;          /* using large sprite size */
    /* Decoded pixel data (up to 64x64) */
    uint8_t pixels[64][64][4]; /* [row][col][RGBA] */
} ExtractedSprite;

/* Full frame extraction result */
typedef struct {
    /* PPU mode for this frame */
    uint8_t mode;
    uint8_t brightness;

    /* Background tiles */
    ExtractedBgTile bg_tiles[MAX_BG_TILES];
    int bg_tile_count;

    /* Sprites */
    ExtractedSprite sprites[MAX_SPRITES];
    int sprite_count;

    /* Raw palette (256 entries, RGBA) */
    uint8_t palette[256][4];

    /* BG scroll positions */
    uint16_t bg_hscroll[4];
    uint16_t bg_vscroll[4];

    /* Which BG layers are enabled */
    bool bg_enabled[4];
    bool sprites_enabled;
} ExtractedFrame;

/*
 * Extract all renderable data from current PPU state.
 * Call after snes_runFrame() completes.
 */
void ppu_extract_frame(const Ppu *ppu, ExtractedFrame *frame);

/*
 * Decode a single 8x8 tile from VRAM at the given address with the given bit depth.
 * palette_base is the CGRAM index offset for this tile's palette.
 */
void ppu_decode_tile(const Ppu *ppu, uint16_t tile_addr, int bit_depth,
                     int palette_base, DecodedTile *out);

#endif /* PPU_EXTRACT_H */
