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
/*  Pre-computed lighting                                              */
/* ------------------------------------------------------------------ */

/* light_dir = normalize(0.3, 0.8, 0.5) */
#define LIGHT_DX  0.30151134457776363f
#define LIGHT_DY  0.80402691887403634f
#define LIGHT_DZ  0.50251890762960605f
#define AMBIENT   0.35f
#define DIFFUSE   0.65f

/* Pre-compute NdotL shading factor for each cube face normal.
 * shade = ambient + diffuse * max(dot(normal, light_dir), 0) */
static float face_shade[6];
static int   shade_inited = 0;

static void init_face_shading(void)
{
    if (shade_inited) return;
    for (int i = 0; i < 6; i++) {
        float ndotl = cube_faces[i].nx * LIGHT_DX +
                      cube_faces[i].ny * LIGHT_DY +
                      cube_faces[i].nz * LIGHT_DZ;
        if (ndotl < 0.0f) ndotl = 0.0f;
        face_shade[i] = AMBIENT + DIFFUSE * ndotl;
    }
    shade_inited = 1;
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
    uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca)
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
            depth_buf[idx] = z;

            int pidx = idx * 4;
            /* RGBA8888 on little-endian: bytes are R,G,B,A but SDL reads as uint32 ABGR */
            color_buf[pidx + 0] = ca;
            color_buf[pidx + 1] = cb;
            color_buf[pidx + 2] = cg;
            color_buf[pidx + 3] = cr;
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
    init_face_shading();

    r->width  = width;
    r->height = height;

    size_t npixels = (size_t)width * (size_t)height;
    r->color_buf = (uint8_t *)malloc(npixels * 4);
    r->depth_buf = (float *)malloc(npixels * sizeof(float));

    if (!r->color_buf || !r->depth_buf) {
        free(r->color_buf);
        free(r->depth_buf);
        memset(r, 0, sizeof(*r));
        return false;
    }
    return true;
}

void soft_renderer_shutdown(SoftRenderer *r)
{
    free(r->color_buf);
    free(r->depth_buf);
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
}

void soft_renderer_draw(SoftRenderer *r, const Camera *cam,
                        const VoxelInstance *voxels, int count)
{
    const int W = r->width;
    const int H = r->height;
    const size_t npixels = (size_t)W * (size_t)H;

    /* Clear color buffer to clear color. ABGR byte order for RGBA8888 on LE. */
    {
        uint8_t *p = r->color_buf;
        uint8_t cr = r->clear_r, cg = r->clear_g, cb = r->clear_b;
        for (size_t i = 0; i < npixels; i++) {
            p[0] = 255; p[1] = cb; p[2] = cg; p[3] = cr;
            p += 4;
        }
    }

    /* Clear depth buffer to 1.0 (far). */
    {
        float *d = r->depth_buf;
        /* Fill with 1.0f.  IEEE 754: 1.0f = 0x3F800000. */
        for (size_t i = 0; i < npixels; i++) {
            d[i] = 1.0f;
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

    init_face_shading();

    /* Process each voxel. */
    for (int vi = 0; vi < count; vi++) {
        const VoxelInstance *v = &voxels[vi];

        /* Skip fully transparent voxels. */
        if (v->a == 0) continue;

        const float vx = v->x;
        const float vy = v->y;
        const float vz = v->z;

        /* Quick reject: if the voxel center is behind the camera in
         * view space, skip it.  We check the clip-space w of the center. */
        {
            float cx = vx + 0.5f;
            float cy = vy + 0.5f;
            float cz = vz + 0.5f;
            Vec4 cc = mat4_transform(mvp, cx, cy, cz);
            if (cc.w <= 0.0f) continue;
        }

        /* Project all 8 corners. */
        ScreenVert sv[8];
        int all_behind = 1;
        for (int ci = 0; ci < 8; ci++) {
            float wx = vx + cube_offsets[ci][0];
            float wy = vy + cube_offsets[ci][1];
            float wz = vz + cube_offsets[ci][2];

            Vec4 clip = mat4_transform(mvp, wx, wy, wz);

            if (clip.w > 0.001f) all_behind = 0;

            /* Perspective divide (guard against w near zero). */
            float iw = (clip.w > 0.001f) ? (1.0f / clip.w) : 0.0f;
            float ndx = clip.x * iw;
            float ndy = clip.y * iw;
            float ndz = clip.z * iw;

            /* NDC -> screen.  NDC y is up, screen y is down. */
            sv[ci].sx = (ndx + 1.0f) * hw;
            sv[ci].sy = (1.0f - ndy) * hh;
            sv[ci].z  = (ndz + 1.0f) * 0.5f; /* map [-1,1] -> [0,1] */
            sv[ci].w  = clip.w;
        }

        if (all_behind) continue;

        /* Screen-space bounding box cull — skip if entirely off-screen */
        {
            float smin_x = sv[0].sx, smax_x = sv[0].sx;
            float smin_y = sv[0].sy, smax_y = sv[0].sy;
            for (int ci = 1; ci < 8; ci++) {
                if (sv[ci].sx < smin_x) smin_x = sv[ci].sx;
                if (sv[ci].sx > smax_x) smax_x = sv[ci].sx;
                if (sv[ci].sy < smin_y) smin_y = sv[ci].sy;
                if (sv[ci].sy > smax_y) smax_y = sv[ci].sy;
            }
            if (smax_x < 0 || smin_x >= W || smax_y < 0 || smin_y >= H) continue;
        }

        /* Center of the voxel in world space — for back-face culling. */
        float cx = vx + 0.5f;
        float cy = vy + 0.5f;
        float cz = vz + 0.5f;

        /* View direction from voxel center toward eye. */
        float vdx = eye_x - cx;
        float vdy = eye_y - cy;
        float vdz = eye_z - cz;

        /* For each of the 6 faces, cull and rasterize. */
        for (int fi = 0; fi < 6; fi++) {
            const CubeFace *face = &cube_faces[fi];

            /* Back-face cull: dot(face_normal, view_dir) <= 0 means away. */
            float ndotv = face->nx * vdx + face->ny * vdy + face->nz * vdz;
            if (ndotv <= 0.0f) continue;

            /* Skip faces if any vertex is behind the near plane.
             * (Proper near-plane clipping would clip the polygon, but
             *  vertices with w<=0 produce garbage screen coordinates.) */
            int behind = 0;
            for (int k = 0; k < 4; k++) {
                if (sv[face->idx[k]].w <= 0.001f) { behind = 1; break; }
            }
            if (behind) continue;

            /* Compute shaded color. */
            float shade = face_shade[fi];
            uint8_t cr = (uint8_t)(clampf(shade * (v->r / 255.0f)) * 255.0f + 0.5f);
            uint8_t cg = (uint8_t)(clampf(shade * (v->g / 255.0f)) * 255.0f + 0.5f);
            uint8_t cb = (uint8_t)(clampf(shade * (v->b / 255.0f)) * 255.0f + 0.5f);
            uint8_t ca = v->a;

            /* Rasterize the quad as two triangles (0-1-2, 0-2-3). */
            const ScreenVert *q0 = &sv[face->idx[0]];
            const ScreenVert *q1 = &sv[face->idx[1]];
            const ScreenVert *q2 = &sv[face->idx[2]];
            const ScreenVert *q3 = &sv[face->idx[3]];

            rasterize_triangle(r->color_buf, r->depth_buf, W, H,
                               q0, q1, q2, cr, cg, cb, ca);
            rasterize_triangle(r->color_buf, r->depth_buf, W, H,
                               q0, q2, q3, cr, cg, cb, ca);
        }
    }
}

const uint8_t *soft_renderer_pixels(const SoftRenderer *r)
{
    return r->color_buf;
}
