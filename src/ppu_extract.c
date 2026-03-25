/*
 * ppu_extract.c — Extract tile/sprite/layer data from LakeSnes PPU state.
 *
 * This is the bridge between emulation and 3D rendering. After each frame,
 * we read the PPU's VRAM, OAM, CGRAM, and register state to produce
 * structured data the voxelizer can consume.
 */

#include "3dsnes/ppu_extract.h"
#include "snes/ppu.h"

#include <string.h>

/* Bit depths per BG layer per mode (from LakeSnes ppu.c) */
static const int bit_depths[8][4] = {
    {2, 2, 2, 2},  /* Mode 0 */
    {4, 4, 2, 0},  /* Mode 1 */
    {4, 4, 0, 0},  /* Mode 2 */
    {8, 4, 0, 0},  /* Mode 3 */
    {8, 2, 0, 0},  /* Mode 4 */
    {4, 2, 0, 0},  /* Mode 5 */
    {4, 0, 0, 0},  /* Mode 6 */
    {8, 0, 0, 0},  /* Mode 7 */
};

/* Sprite sizes: [objSize][0=small, 1=large] */
static const int sprite_sizes[8][2] = {
    {8, 16}, {8, 32}, {8, 64}, {16, 32},
    {16, 64}, {32, 64}, {16, 32}, {16, 32}
};

/* Convert 15-bit SNES color to RGBA */
static void snes_color_to_rgba(uint16_t color, uint8_t *rgba) {
    uint8_t r5 = color & 0x1f;
    uint8_t g5 = (color >> 5) & 0x1f;
    uint8_t b5 = (color >> 10) & 0x1f;
    rgba[0] = (r5 << 3) | (r5 >> 2);
    rgba[1] = (g5 << 3) | (g5 >> 2);
    rgba[2] = (b5 << 3) | (b5 >> 2);
    rgba[3] = 255;
}

/* Extract the full 256-color palette from CGRAM */
static void extract_palette(const Ppu *ppu, ExtractedFrame *frame) {
    for (int i = 0; i < 256; i++) {
        snes_color_to_rgba(ppu->cgram[i], frame->palette[i]);
    }
}

void ppu_decode_tile(const Ppu *ppu, uint16_t tile_addr, int bit_depth,
                     int palette_base, DecodedTile *out)
{
    memset(out, 0, sizeof(*out));

    for (int row = 0; row < 8; row++) {
        /* Plane 1 (2bpp, always present) */
        uint16_t plane1 = ppu->vram[(tile_addr + row) & 0x7fff];
        /* Plane 2 (4bpp+) */
        uint16_t plane2 = 0;
        if (bit_depth > 2) {
            plane2 = ppu->vram[(tile_addr + 8 + row) & 0x7fff];
        }
        /* Plane 3+4 (8bpp) */
        uint16_t plane3 = 0, plane4 = 0;
        if (bit_depth > 4) {
            plane3 = ppu->vram[(tile_addr + 16 + row) & 0x7fff];
            plane4 = ppu->vram[(tile_addr + 24 + row) & 0x7fff];
        }

        for (int col = 0; col < 8; col++) {
            int bit = 7 - col;
            int pixel = 0;
            pixel |= ((plane1 >> bit) & 1);
            pixel |= ((plane1 >> (8 + bit)) & 1) << 1;
            if (bit_depth > 2) {
                pixel |= ((plane2 >> bit) & 1) << 2;
                pixel |= ((plane2 >> (8 + bit)) & 1) << 3;
            }
            if (bit_depth > 4) {
                pixel |= ((plane3 >> bit) & 1) << 4;
                pixel |= ((plane3 >> (8 + bit)) & 1) << 5;
                pixel |= ((plane4 >> bit) & 1) << 6;
                pixel |= ((plane4 >> (8 + bit)) & 1) << 7;
            }

            if (pixel == 0) {
                /* Transparent */
                out->pixels[row][col][0] = 0;
                out->pixels[row][col][1] = 0;
                out->pixels[row][col][2] = 0;
                out->pixels[row][col][3] = 0;
            } else {
                int cgram_idx = palette_base + pixel;
                uint16_t color = ppu->cgram[cgram_idx & 0xff];
                snes_color_to_rgba(color, out->pixels[row][col]);
            }
        }
    }
}

/* Extract visible BG tiles for one layer */
static void extract_bg_layer(const Ppu *ppu, int layer, ExtractedFrame *frame) {
    int mode = ppu->mode;
    if (mode >= 8) return; /* safety */

    int bpp = bit_depths[mode][layer];
    if (bpp == 0) return; /* layer not active in this mode */

    if (!ppu->layer[layer].mainScreenEnabled) return;

    const BgLayer *bg = &ppu->bgLayer[layer];
    bool big_tiles = bg->bigTiles;
    int tile_w = big_tiles ? 16 : 8;
    int tile_h = big_tiles ? 16 : 8;

    /* Visible area: 256x224 pixels */
    int screen_w = 256;
    int screen_h = 224;

    /* Walk through visible tile grid positions */
    for (int sy = 0; sy < screen_h; sy += tile_h) {
        for (int sx = 0; sx < screen_w; sx += tile_w) {
            if (frame->bg_tile_count >= MAX_BG_TILES) return;

            /* Apply scroll to get VRAM coordinates */
            int vx = (sx + bg->hScroll) & 0x3ff;
            int vy = (sy + bg->vScroll) & 0x3ff;

            /* Calculate tilemap address */
            int tile_bits_x = big_tiles ? 4 : 3;
            int tile_bits_y = big_tiles ? 4 : 3;
            int tile_high_x = big_tiles ? 0x200 : 0x100;
            int tile_high_y = big_tiles ? 0x200 : 0x100;

            uint16_t tmap_adr = bg->tilemapAdr +
                (((vy >> tile_bits_y) & 0x1f) << 5) +
                ((vx >> tile_bits_x) & 0x1f);

            if ((vx & tile_high_x) && bg->tilemapWider)
                tmap_adr += 0x400;
            if ((vy & tile_high_y) && bg->tilemapHigher)
                tmap_adr += bg->tilemapWider ? 0x800 : 0x400;

            uint16_t tile_word = ppu->vram[tmap_adr & 0x7fff];

            int tile_num = tile_word & 0x3ff;
            int palette_num = (tile_word >> 10) & 7;
            bool hflip = (tile_word >> 14) & 1;
            bool vflip = (tile_word >> 15) & 1;
            int prio = (tile_word >> 13) & 1;

            /* Palette base in CGRAM */
            int palette_size = (bpp == 2) ? 4 : (bpp == 4) ? 16 : 256;
            int palette_base = palette_size * palette_num;
            if (mode == 0) palette_base += palette_size * 4 * layer;

            /* Tile data address in VRAM */
            uint16_t tile_addr = bg->tileAdr + (tile_num * 4 * bpp);

            /* Decode the tile */
            ExtractedBgTile *bt = &frame->bg_tiles[frame->bg_tile_count];
            bt->screen_x = sx;
            bt->screen_y = sy;
            bt->bg_layer = layer;
            bt->priority = prio;
            bt->tile_num = tile_num;
            bt->palette_num = palette_num;
            bt->hflip = hflip;
            bt->vflip = vflip;

            /* For 16x16 tiles, decode all four 8x8 sub-tiles into the decoded buffer */
            if (big_tiles) {
                /* Decode 4 sub-tiles into the 8x8 decoded grid
                   (we'll just decode top-left for now, the voxelizer handles 16x16) */
                /* Top-left */
                DecodedTile sub;
                int sub_tiles[4];
                sub_tiles[0] = tile_num;
                sub_tiles[1] = tile_num + 1;
                sub_tiles[2] = tile_num + 0x10;
                sub_tiles[3] = tile_num + 0x10 + 1;

                /* Just decode the first 8x8 subtile into bt->decoded for now */
                uint16_t sub_addr = bg->tileAdr + (sub_tiles[0] * 4 * bpp);
                ppu_decode_tile(ppu, sub_addr, bpp, palette_base, &bt->decoded);
            } else {
                ppu_decode_tile(ppu, tile_addr, bpp, palette_base, &bt->decoded);
            }

            /* Apply flip to decoded pixels */
            if (hflip || vflip) {
                DecodedTile tmp;
                memcpy(&tmp, &bt->decoded, sizeof(tmp));
                for (int r = 0; r < 8; r++) {
                    for (int c = 0; c < 8; c++) {
                        int sr = vflip ? (7 - r) : r;
                        int sc = hflip ? (7 - c) : c;
                        memcpy(bt->decoded.pixels[r][c], tmp.pixels[sr][sc], 4);
                    }
                }
            }

            frame->bg_tile_count++;
        }
    }
}

/* Extract sprites from OAM */
static void extract_sprites(const Ppu *ppu, ExtractedFrame *frame) {
    int small_size = sprite_sizes[ppu->objSize][0];
    int large_size = sprite_sizes[ppu->objSize][1];

    for (int i = 0; i < 128; i++) {
        if (frame->sprite_count >= MAX_SPRITES) break;

        /* Read OAM entry (4 bytes per sprite stored as 2 words) */
        int idx = i * 2;
        uint16_t w0 = ppu->oam[idx];
        uint16_t w1 = ppu->oam[idx + 1];

        int x = w0 & 0xff;
        int y = (w0 >> 8) & 0xff;
        int tile = w1 & 0xff;
        int name_table = (w1 >> 8) & 1;
        int palette = (w1 >> 9) & 7;
        int prio = (w1 >> 12) & 3;
        bool hflip = (w1 >> 14) & 1;
        bool vflip = (w1 >> 15) & 1;

        /* High OAM bits */
        int high_byte = ppu->highOam[i >> 2];
        int high_bit = (i & 3) * 2;
        int x_high = (high_byte >> high_bit) & 1;
        int size_bit = (high_byte >> (high_bit + 1)) & 1;

        /* Full X position (9-bit signed) */
        if (x_high) x |= 0x100;
        if (x >= 256) x -= 512;

        int sprite_w = size_bit ? large_size : small_size;
        int sprite_h = sprite_w; /* SNES sprites are square */

        /* Skip offscreen sprites */
        if (x <= -sprite_w || x >= 256 || y >= 224) continue;
        /* Y=0 could be valid, skip truly invisible */
        if (y >= 224 && y < 240) continue;

        ExtractedSprite *sp = &frame->sprites[frame->sprite_count];
        sp->screen_x = x;
        sp->screen_y = y;
        sp->width = sprite_w;
        sp->height = sprite_h;
        sp->priority = prio;
        sp->palette_num = palette;
        sp->tile_num = tile;
        sp->hflip = hflip;
        sp->vflip = vflip;
        sp->large = size_bit;

        memset(sp->pixels, 0, sizeof(sp->pixels));

        /* Decode sprite tiles (always 4bpp for sprites) */
        uint16_t base_addr = name_table ? ppu->objTileAdr2 : ppu->objTileAdr1;

        for (int ty = 0; ty < sprite_h; ty += 8) {
            for (int tx = 0; tx < sprite_w; tx += 8) {
                /* Calculate sub-tile number within sprite grid */
                int col_off = tx / 8;
                int row_off = ty / 8;

                /* SNES sprite tiles are arranged in a 16x16 grid in VRAM */
                int sub_tile = ((((tile >> 4) + row_off) & 0xf) << 4) |
                               (((tile & 0xf) + col_off) & 0xf);

                uint16_t tile_addr = base_addr + sub_tile * 16;

                /* Sprites always use 4bpp, palettes 128-255 (OBJ palettes) */
                int palette_base = 128 + palette * 16;

                DecodedTile decoded;
                ppu_decode_tile(ppu, tile_addr, 4, palette_base, &decoded);

                /* Copy into sprite pixel buffer with flip handling */
                for (int r = 0; r < 8; r++) {
                    for (int c = 0; c < 8; c++) {
                        int dst_r = vflip ? (sprite_h - 1 - (ty + r)) : (ty + r);
                        int dst_c = hflip ? (sprite_w - 1 - (tx + c)) : (tx + c);
                        if (dst_r >= 0 && dst_r < 64 && dst_c >= 0 && dst_c < 64) {
                            memcpy(sp->pixels[dst_r][dst_c], decoded.pixels[r][c], 4);
                        }
                    }
                }
            }
        }

        frame->sprite_count++;
    }
}

void ppu_extract_frame(const Ppu *ppu, ExtractedFrame *frame) {
    memset(frame, 0, sizeof(*frame));

    frame->mode = ppu->mode;
    frame->brightness = ppu->brightness;

    /* Extract palette */
    extract_palette(ppu, frame);

    /* Record scroll positions and enabled state */
    for (int i = 0; i < 4; i++) {
        frame->bg_hscroll[i] = ppu->bgLayer[i].hScroll;
        frame->bg_vscroll[i] = ppu->bgLayer[i].vScroll;
        frame->bg_enabled[i] = ppu->layer[i].mainScreenEnabled;
    }
    frame->sprites_enabled = ppu->layer[4].mainScreenEnabled;

    /* Extract BG layers */
    for (int layer = 0; layer < 4; layer++) {
        extract_bg_layer(ppu, layer, frame);
    }

    /* Extract sprites */
    if (frame->sprites_enabled) {
        extract_sprites(ppu, frame);
    }
}
