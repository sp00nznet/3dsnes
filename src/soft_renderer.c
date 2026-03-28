/*
 * soft_renderer.c — Software 3D voxel renderer implementation.
 *
 * For each voxel cube, we:
 *   1. Compute 8 corners of the unit cube at (x, y, z)
 *   2. Transform through the combined view*proj matrix
 *   3. Back-face cull using face normal dot eye direction
 *   4. Rasterize visible faces as two triangles each
 *   5. Z-buffer and write shaded color to the RGBA pixel buffer
 *
 * Lighting matches the GL shader:
 *   light_dir = normalize(0.3, 0.8, 0.5)
 *   ambient   = 0.35
 *   diffuse   = 0.65
 */

#include "3dsnes/soft_renderer.h"
#include "3dsnes/fxaa.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/* Clamp an integer to [lo, hi]. */
static inline int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Clamp a float to [0, 1]. */
static inline float clampf(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/* Min / max of two ints. */
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }

/* Min / max of three floats. */
static inline float minf3(float a, float b, float c)
{
    float m = a < b ? a : b;
    return m < c ? m : c;
}
static inline float maxf3(float a, float b, float c)
{
    float m = a > b ? a : b;
    return m > c ? m : c;
}

/* ------------------------------------------------------------------ */
/*  4x4 matrix multiply (column-major)                                */
/* ------------------------------------------------------------------ */

static void mat4_mul(float out[16], const float a[16], const float b[16])
{
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            out[c * 4 + r] =
                a[0 * 4 + r] * b[c * 4 + 0] +
                a[1 * 4 + r] * b[c * 4 + 1] +
                a[2 * 4 + r] * b[c * 4 + 2] +
                a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
}

/* Transform a point (x,y,z,1) through a 4x4 column-major matrix.
 * Returns clip-space (x, y, z, w). */
typedef struct { float x, y, z, w; } Vec4;

static inline Vec4 mat4_transform(const float m[16], float x, float y, float z)
{
    Vec4 o;
    o.x = m[0]*x + m[4]*y + m[ 8]*z + m[12];
    o.y = m[1]*x + m[5]*y + m[ 9]*z + m[13];
    o.z = m[2]*x + m[6]*y + m[10]*z + m[14];
    o.w = m[3]*x + m[7]*y + m[11]*z + m[15];
    return o;
}

/* ------------------------------------------------------------------ */
/*  Projected screen-space vertex                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    float sx, sy;   /* screen x, y */
    float z;        /* NDC depth for z-buffer (0 = near, 1 = far) */
    float w;        /* clip w (positive when in front of camera) */
} ScreenVert;

/* ------------------------------------------------------------------ */
/*  Cube faces                                                         */
/* ------------------------------------------------------------------ */

/*
 * Unit cube corners (VOXEL_SIZE = 1):
 *   0: (0,0,0)  1: (1,0,0)  2: (1,1,0)  3: (0,1,0)
 *   4: (0,0,1)  5: (1,0,1)  6: (1,1,1)  7: (0,1,1)
 */
static const float cube_offsets[8][3] = {
    {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0},
    {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1},
};

/* Each face as 4 vertex indices (CCW winding when viewed from outside)
 * and its outward normal. */
typedef struct {
    int idx[4];       /* vertex indices into cube_offsets */
    float nx, ny, nz; /* outward face normal */
} CubeFace;

static const CubeFace cube_faces[6] = {
    /* +Z face (front)  */ { {4, 5, 6, 7},  0,  0,  1 },
    /* -Z face (back)   */ { {1, 0, 3, 2},  0,  0, -1 },
    /* +X face (right)  */ { {1, 2, 6, 5},  1,  0,  0 },
    /* -X face (left)   */ { {0, 4, 7, 3}, -1,  0,  0 },
    /* +Y face (top)    */ { {3, 7, 6, 2},  0,  1,  0 },
    /* -Y face (bottom) */ { {0, 1, 5, 4},  0, -1,  0 },
};

/* ------------------------------------------------------------------ */
/*  Configurable lighting                                              */
/* ------------------------------------------------------------------ */

void soft_renderer_set_lighting(SoftRenderer *r, float lx, float ly, float lz,
                                float ambient, float diffuse)
{
    /* Normalize the direction vector */
    float len = sqrtf(lx * lx + ly * ly + lz * lz);
    if (len < 1e-6f) len = 1.0f;
    float inv_len = 1.0f / len;
    r->light_dir[0] = lx * inv_len;
    r->light_dir[1] = ly * inv_len;
    r->light_dir[2] = lz * inv_len;
    r->ambient = ambient;
    r->diffuse = diffuse;

    /* Pre-compute NdotL shading factor for each cube face normal.
     * shade = ambient + diffuse * max(dot(normal, light_dir), 0) */
    for (int i = 0; i < 6; i++) {
        float ndotl = cube_faces[i].nx * r->light_dir[0] +
                      cube_faces[i].ny * r->light_dir[1] +
                      cube_faces[i].nz * r->light_dir[2];
        if (ndotl < 0.0f) ndotl = 0.0f;
        r->face_shade[i] = r->ambient + r->diffuse * ndotl;
    }
}

/* ------------------------------------------------------------------ */
/*  Triangle rasterizer                                                */
/* ------------------------------------------------------------------ */

/*
 * Edge function for a 2D triangle: positive if (px,py) is on the
 * left side of the edge from (ax,ay) to (bx,by).
 * Uses fixed-point (16.8) to avoid float precision issues in the
 * inner loop.
 */

/* We use floats for the edge setup but fixed-point increments. */

/* Rasterize one triangle with z-buffer and flat shading.
 * v0, v1, v2 are screen-space vertices (CCW winding expected). */
static void rasterize_triangle(
    uint8_t *color_buf, float *depth_buf, int buf_w, int buf_h,
    const ScreenVert *v0, const ScreenVert *v1, const ScreenVert *v2,
    uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca,
    bool write_depth)
{
    /* Bounding box (screen pixels). */
    float fminx = minf3(v0->sx, v1->sx, v2->sx);
    float fminy = minf3(v0->sy, v1->sy, v2->sy);
    float fmaxx = maxf3(v0->sx, v1->sx, v2->sx);
    float fmaxy = maxf3(v0->sy, v1->sy, v2->sy);

    int minx = clampi((int)floorf(fminx), 0, buf_w - 1);
    int miny = clampi((int)floorf(fminy), 0, buf_h - 1);
    int maxx = clampi((int)ceilf(fmaxx),  0, buf_w - 1);
    int maxy = clampi((int)ceilf(fmaxy),  0, buf_h - 1);

    /* Edge equation setup.
     * For edge (A -> B), the function is: E(P) = (B.x-A.x)*(P.y-A.y) - (B.y-A.y)*(P.x-A.x)
     * Positive inside for CCW winding. */
    float e0_dx = v1->sx - v0->sx;
    float e0_dy = v1->sy - v0->sy;
    float e1_dx = v2->sx - v1->sx;
    float e1_dy = v2->sy - v1->sy;
    float e2_dx = v0->sx - v2->sx;
    float e2_dy = v0->sy - v2->sy;

    /* Twice the signed area — used for barycentric normalization. */
    float area2 = e0_dx * (v2->sy - v0->sy) - e0_dy * (v2->sx - v0->sx);
    /* Accept both CW and CCW winding (screen Y-flip reverses winding) */
    if (area2 < 0.0f) {
        area2 = -area2;
        /* Swap v1 and v2 to fix winding */
        const ScreenVert *tmp = v1; v1 = v2; v2 = tmp;
        e0_dx = v1->sx - v0->sx; e0_dy = v1->sy - v0->sy;
        e1_dx = v2->sx - v1->sx; e1_dy = v2->sy - v1->sy;
        e2_dx = v0->sx - v2->sx; e2_dy = v0->sy - v2->sy;
    }
    if (area2 < 0.001f) return; /* degenerate */

    float inv_area2 = 1.0f / area2;

    /* Scan the bounding box. */
    for (int py = miny; py <= maxy; py++) {
        float pfy = (float)py + 0.5f;
        for (int px = minx; px <= maxx; px++) {
            float pfx = (float)px + 0.5f;

            /* Edge functions at pixel center. */
            float w0 = e1_dx * (pfy - v1->sy) - e1_dy * (pfx - v1->sx);
            float w1 = e2_dx * (pfy - v2->sy) - e2_dy * (pfx - v2->sx);
            float w2 = e0_dx * (pfy - v0->sy) - e0_dy * (pfx - v0->sx);

            /* Inside test (top-left rule not critical for voxels). */
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

            /* Barycentric coordinates. */
            float b0 = w0 * inv_area2;
            float b1 = w1 * inv_area2;
            float b2 = w2 * inv_area2;

            /* Interpolate depth. */
            float z = b0 * v0->z + b1 * v1->z + b2 * v2->z;

            /* Z-buffer test (less = closer). */
            int idx = py * buf_w + px;
            if (z >= depth_buf[idx]) continue;
            if (write_depth) depth_buf[idx] = z;

            int pidx = idx * 4;
            if (ca < 255 && !write_depth) {
                /* Alpha blending (back-to-front transparent pass) */
                float af = ca / 255.0f;
                float inv_af = 1.0f - af;
                color_buf[pidx + 0] = 0xFF; /* alpha stays opaque in output */
                color_buf[pidx + 1] = (uint8_t)(cb * af + color_buf[pidx + 1] * inv_af);
                color_buf[pidx + 2] = (uint8_t)(cg * af + color_buf[pidx + 2] * inv_af);
                color_buf[pidx + 3] = (uint8_t)(cr * af + color_buf[pidx + 3] * inv_af);
            } else {
                /* Opaque write */
                color_buf[pidx + 0] = ca;
                color_buf[pidx + 1] = cb;
                color_buf[pidx + 2] = cg;
                color_buf[pidx + 3] = cr;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

bool soft_renderer_init(SoftRenderer *r, int width, int height)
{
    memset(r, 0, sizeof(*r));
    r->clear_r = 20;
    r->clear_g = 20;
    r->clear_b = 31;
    soft_renderer_set_lighting(r, 0.3f, 0.8f, 0.5f, 0.35f, 0.65f);

    r->width  = width;
    r->height = height;

    size_t npixels = (size_t)width * (size_t)height;
    r->color_buf = (uint8_t *)malloc(npixels * 4);
    r->depth_buf = (float *)malloc(npixels * sizeof(float));
    r->shadow_buf = (float *)malloc(npixels * sizeof(float));
    r->post_buf  = (uint8_t *)malloc(npixels * 4);

    if (!r->color_buf || !r->depth_buf) {
        free(r->color_buf);
        free(r->depth_buf);
        free(r->shadow_buf);
        free(r->post_buf);
        memset(r, 0, sizeof(*r));
        return false;
    }
    r->shadows_enabled = false;
    r->shadow_opacity = 0.5f;
    r->shadow_y = 0.0f;
    r->fxaa_enabled = false;
    r->sky_type = 0;
    r->sky_top_r = 40; r->sky_top_g = 40; r->sky_top_b = 80;
    r->sky_bot_r = 10; r->sky_bot_g = 10; r->sky_bot_b = 20;
    return true;
}

void soft_renderer_shutdown(SoftRenderer *r)
{
    free(r->color_buf);
    free(r->depth_buf);
    free(r->shadow_buf);
    free(r->post_buf);
    memset(r, 0, sizeof(*r));
}

void soft_renderer_resize(SoftRenderer *r, int width, int height)
{
    if (width == r->width && height == r->height) return;

    size_t npixels = (size_t)width * (size_t)height;
    uint8_t *new_color = (uint8_t *)realloc(r->color_buf, npixels * 4);
    float   *new_depth = (float *)realloc(r->depth_buf, npixels * sizeof(float));

    if (!new_color || !new_depth) {
        /* Keep old buffers on failure. */
        if (new_color && new_color != r->color_buf) free(new_color);
        if (new_depth && new_depth != r->depth_buf) free(new_depth);
        return;
    }
    r->color_buf = new_color;
    r->depth_buf = new_depth;
    r->width  = width;
    r->height = height;

    /* Realloc shadow buffer to match new size */
    float *new_shadow = (float *)realloc(r->shadow_buf, npixels * sizeof(float));
    if (new_shadow) {
        r->shadow_buf = new_shadow;
    }

    /* Realloc FXAA post-processing buffer */
    uint8_t *new_post = (uint8_t *)realloc(r->post_buf, npixels * 4);
    if (new_post) {
        r->post_buf = new_post;
    }
}

void soft_renderer_draw(SoftRenderer *r, const Camera *cam,
                        const VoxelInstance *voxels, int count)
{
    const int W = r->width;
    const int H = r->height;
    const size_t npixels = (size_t)W * (size_t)H;

    /* Clear color buffer — gradient or solid fill. */
    if (r->sky_type == 1 && H > 1) {
        /* Vertical gradient clear */
        for (int y = 0; y < H; y++) {
            float t = (float)y / (float)(H - 1);
            uint8_t gr = (uint8_t)(r->sky_top_r * (1.0f - t) + r->sky_bot_r * t);
            uint8_t gg = (uint8_t)(r->sky_top_g * (1.0f - t) + r->sky_bot_g * t);
            uint8_t gb = (uint8_t)(r->sky_top_b * (1.0f - t) + r->sky_bot_b * t);
            uint32_t val = (uint32_t)gr << 24 | (uint32_t)gg << 16 |
                           (uint32_t)gb << 8  | 0xFF;
            uint32_t *row = (uint32_t *)r->color_buf + y * W;
            for (int x = 0; x < W; x++) row[x] = val;
        }
    } else {
        /* Solid color clear. ABGR byte order for RGBA8888 on LE. */
        uint32_t clear_val = (uint32_t)r->clear_r << 24 |
                             (uint32_t)r->clear_g << 16 |
                             (uint32_t)r->clear_b << 8  | 0xFF;
        uint32_t *p = (uint32_t *)r->color_buf;
        for (size_t i = 0; i < npixels; i++) {
            p[i] = clear_val;
        }
    }

    /* Clear depth buffer. IEEE 754: 1.0f = 0x3F800000, use memset-compatible fill. */
    {
        uint32_t *d = (uint32_t *)r->depth_buf;
        for (size_t i = 0; i < npixels; i++) {
            d[i] = 0x3F800000;
        }
    }

    if (count <= 0) return;

    /* Cap voxel count for real-time performance on CPU */
    if (count > 300000) count = 300000;

    /* Combined view * projection matrix. */
    float mvp[16];
    mat4_mul(mvp, cam->proj, cam->view);

    /* Half-viewport for NDC -> screen conversion. */
    const float hw = (float)W * 0.5f;
    const float hh = (float)H * 0.5f;

    /* Eye position for back-face culling. */
    const float eye_x = cam->eye_x;
    const float eye_y = cam->eye_y;
    const float eye_z = cam->eye_z;

    /* ---- Helper: render a single voxel ---- */
    #define RENDER_VOXEL(vi_idx, do_write_depth) \
    do { \
        const VoxelInstance *v = &voxels[(vi_idx)]; \
        const float vx = v->x; \
        const float vy = v->y; \
        const float vz = v->z; \
        /* Quick reject: clip-space bounds check on voxel center. */ \
        { \
            float cx_ = vx + 0.5f; \
            float cy_ = vy + 0.5f; \
            float cz_ = vz + 0.5f; \
            Vec4 cc = mat4_transform(mvp, cx_, cy_, cz_); \
            if (cc.w <= 0.0f) break; \
            float iw_ = 1.0f / cc.w; \
            float ndx_ = cc.x * iw_; \
            float ndy_ = cc.y * iw_; \
            if (ndx_ < -1.5f || ndx_ > 1.5f || ndy_ < -1.5f || ndy_ > 1.5f) break; \
        } \
        /* Project all 8 corners. */ \
        ScreenVert sv[8]; \
        int all_behind = 1; \
        for (int ci = 0; ci < 8; ci++) { \
            float wx = vx + cube_offsets[ci][0]; \
            float wy = vy + cube_offsets[ci][1]; \
            float wz = vz + cube_offsets[ci][2]; \
            Vec4 clip = mat4_transform(mvp, wx, wy, wz); \
            if (clip.w > 0.001f) all_behind = 0; \
            float iw_ = (clip.w > 0.001f) ? (1.0f / clip.w) : 0.0f; \
            float ndx_ = clip.x * iw_; \
            float ndy_ = clip.y * iw_; \
            float ndz_ = clip.z * iw_; \
            sv[ci].sx = (ndx_ + 1.0f) * hw; \
            sv[ci].sy = (1.0f - ndy_) * hh; \
            sv[ci].z  = (ndz_ + 1.0f) * 0.5f; \
            sv[ci].w  = clip.w; \
        } \
        if (all_behind) break; \
        /* Screen-space bounding box cull */ \
        { \
            float smin_x = sv[0].sx, smax_x = sv[0].sx; \
            float smin_y = sv[0].sy, smax_y = sv[0].sy; \
            for (int ci = 1; ci < 8; ci++) { \
                if (sv[ci].sx < smin_x) smin_x = sv[ci].sx; \
                if (sv[ci].sx > smax_x) smax_x = sv[ci].sx; \
                if (sv[ci].sy < smin_y) smin_y = sv[ci].sy; \
                if (sv[ci].sy > smax_y) smax_y = sv[ci].sy; \
            } \
            if (smax_x < 0 || smin_x >= W || smax_y < 0 || smin_y >= H) break; \
        } \
        float cx_ = vx + 0.5f; \
        float cy_ = vy + 0.5f; \
        float cz_ = vz + 0.5f; \
        float vdx = eye_x - cx_; \
        float vdy = eye_y - cy_; \
        float vdz = eye_z - cz_; \
        for (int fi = 0; fi < 6; fi++) { \
            const CubeFace *face = &cube_faces[fi]; \
            float ndotv = face->nx * vdx + face->ny * vdy + face->nz * vdz; \
            if (ndotv <= 0.0f) continue; \
            int behind = 0; \
            for (int k = 0; k < 4; k++) { \
                if (sv[face->idx[k]].w <= 0.001f) { behind = 1; break; } \
            } \
            if (behind) continue; \
            float shade = r->face_shade[fi]; \
            uint8_t cr = (uint8_t)(clampf(shade * (v->r / 255.0f)) * 255.0f + 0.5f); \
            uint8_t cg = (uint8_t)(clampf(shade * (v->g / 255.0f)) * 255.0f + 0.5f); \
            uint8_t cb = (uint8_t)(clampf(shade * (v->b / 255.0f)) * 255.0f + 0.5f); \
            uint8_t ca = v->a; \
            const ScreenVert *q0 = &sv[face->idx[0]]; \
            const ScreenVert *q1 = &sv[face->idx[1]]; \
            const ScreenVert *q2 = &sv[face->idx[2]]; \
            const ScreenVert *q3 = &sv[face->idx[3]]; \
            rasterize_triangle(r->color_buf, r->depth_buf, W, H, \
                               q0, q1, q2, cr, cg, cb, ca, (do_write_depth)); \
            rasterize_triangle(r->color_buf, r->depth_buf, W, H, \
                               q0, q2, q3, cr, cg, cb, ca, (do_write_depth)); \
        } \
    } while (0)

    /* ---- Pass 1: opaque voxels ---- */
    for (int vi = 0; vi < count; vi++) {
        if (voxels[vi].a == 0 || voxels[vi].a < 255) continue;
        RENDER_VOXEL(vi, true);
    }

    /* ---- Pass 2: transparent voxels (sorted back-to-front) ---- */
    {
        /* Build index list of transparent voxels */
        int *trans_idx = NULL;
        float *trans_dist = NULL;
        int trans_count = 0;

        /* Count first */
        for (int vi = 0; vi < count; vi++) {
            if (voxels[vi].a > 0 && voxels[vi].a < 255) trans_count++;
        }

        if (trans_count > 0) {
            trans_idx = (int *)malloc(sizeof(int) * trans_count);
            trans_dist = (float *)malloc(sizeof(float) * trans_count);
            if (trans_idx && trans_dist) {
                int ti = 0;
                for (int vi = 0; vi < count; vi++) {
                    if (voxels[vi].a > 0 && voxels[vi].a < 255) {
                        float dx = (voxels[vi].x + 0.5f) - eye_x;
                        float dy = (voxels[vi].y + 0.5f) - eye_y;
                        float dz = (voxels[vi].z + 0.5f) - eye_z;
                        trans_idx[ti] = vi;
                        trans_dist[ti] = dx * dx + dy * dy + dz * dz;
                        ti++;
                    }
                }

                /* Sort back-to-front (greatest distance first) using insertion sort
                 * for simplicity — transparent voxel count is typically small */
                for (int i = 1; i < trans_count; i++) {
                    float key_dist = trans_dist[i];
                    int key_idx = trans_idx[i];
                    int j = i - 1;
                    while (j >= 0 && trans_dist[j] < key_dist) {
                        trans_dist[j + 1] = trans_dist[j];
                        trans_idx[j + 1] = trans_idx[j];
                        j--;
                    }
                    trans_dist[j + 1] = key_dist;
                    trans_idx[j + 1] = key_idx;
                }

                /* Render sorted transparent voxels */
                for (int i = 0; i < trans_count; i++) {
                    RENDER_VOXEL(trans_idx[i], false);
                }
            }
            free(trans_idx);
            free(trans_dist);
        }
    }

    #undef RENDER_VOXEL

    /* ---- Shadow projection pass ---- */
    if (r->shadows_enabled && r->shadow_buf) {
        /* Clear shadow buffer */
        memset(r->shadow_buf, 0, npixels * sizeof(float));

        float ly = r->light_dir[1];
        if (fabsf(ly) < 0.01f) ly = 0.01f; /* avoid div by zero */

        /* For each opaque voxel, project shadow onto ground plane */
        for (int vi = 0; vi < count; vi++) {
            const VoxelInstance *v = &voxels[vi];
            if (v->a == 0) continue;

            float vy = v->y;
            if (vy <= r->shadow_y) continue; /* below ground, no shadow */

            /* Project voxel center onto ground plane along light direction */
            float t = (vy + 0.5f - r->shadow_y) / ly;
            float shadow_x = v->x + 0.5f - r->light_dir[0] * t;
            float shadow_z = v->z + 0.5f - r->light_dir[2] * t;

            /* Project shadow position to screen space via MVP */
            Vec4 clip = mat4_transform(mvp, shadow_x, r->shadow_y, shadow_z);
            if (clip.w <= 0.001f) continue;
            float iw = 1.0f / clip.w;
            int sx = (int)((clip.x * iw + 1.0f) * hw);
            int sy = (int)((1.0f - clip.y * iw) * hh);

            /* Paint a small shadow footprint (3x3 pixels) */
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int px = sx + dx;
                    int py = sy + dy;
                    if (px >= 0 && px < W && py >= 0 && py < H) {
                        int sidx = py * W + px;
                        r->shadow_buf[sidx] += 0.3f;
                        if (r->shadow_buf[sidx] > 1.0f) r->shadow_buf[sidx] = 1.0f;
                    }
                }
            }
        }

        /* Apply shadows to color buffer */
        {
            float opacity = r->shadow_opacity;
            uint8_t *cbuf = r->color_buf;
            for (size_t i = 0; i < npixels; i++) {
                float s = r->shadow_buf[i];
                if (s <= 0.0f) continue;
                float darken = 1.0f - s * opacity;
                if (darken < 0.2f) darken = 0.2f;
                int pidx = (int)i * 4;
                /* bytes: [0]=A, [1]=B, [2]=G, [3]=R */
                cbuf[pidx + 1] = (uint8_t)(cbuf[pidx + 1] * darken);
                cbuf[pidx + 2] = (uint8_t)(cbuf[pidx + 2] * darken);
                cbuf[pidx + 3] = (uint8_t)(cbuf[pidx + 3] * darken);
            }
        }
    }

    /* FXAA post-processing pass */
    if (r->fxaa_enabled && r->post_buf) {
        fxaa_apply(r->color_buf, r->post_buf, W, H);
    }
}

const uint8_t *soft_renderer_pixels(const SoftRenderer *r)
{
    if (r->fxaa_enabled && r->post_buf)
        return r->post_buf;
    return r->color_buf;
}
