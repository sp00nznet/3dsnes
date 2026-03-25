/*
 * camera.c — Orbit camera for 3D SNES scene viewing.
 */

#include "3dsnes/camera.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float deg2rad(float deg) { return deg * (float)(M_PI / 180.0); }

/* Build a look-at view matrix (column-major) */
static void mat4_lookat(float *m, float ex, float ey, float ez,
                        float tx, float ty, float tz,
                        float ux, float uy, float uz)
{
    float fx = tx - ex, fy = ty - ey, fz = tz - ez;
    float fl = sqrtf(fx*fx + fy*fy + fz*fz);
    if (fl > 0.0001f) { fx /= fl; fy /= fl; fz /= fl; }

    /* side = forward x up */
    float sx = fy*uz - fz*uy;
    float sy = fz*ux - fx*uz;
    float sz = fx*uy - fy*ux;
    float sl = sqrtf(sx*sx + sy*sy + sz*sz);
    if (sl > 0.0001f) { sx /= sl; sy /= sl; sz /= sl; }

    /* recompute up = side x forward */
    ux = sy*fz - sz*fy;
    uy = sz*fx - sx*fz;
    uz = sx*fy - sy*fx;

    memset(m, 0, 16 * sizeof(float));
    m[0] = sx;   m[4] = sy;   m[8]  = sz;   m[12] = -(sx*ex + sy*ey + sz*ez);
    m[1] = ux;   m[5] = uy;   m[9]  = uz;   m[13] = -(ux*ex + uy*ey + uz*ez);
    m[2] = -fx;  m[6] = -fy;  m[10] = -fz;  m[14] = (fx*ex + fy*ey + fz*ez);
    m[3] = 0;    m[7] = 0;    m[11] = 0;    m[15] = 1;
}

/* Build a perspective projection matrix (column-major) */
static void mat4_perspective(float *m, float fov_deg, float aspect,
                             float near, float far)
{
    float f = 1.0f / tanf(deg2rad(fov_deg) * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
}

void camera_init(Camera *cam, float aspect) {
    /*
     * Diorama coordinate system:
     *   X = SNES horizontal (0..255)
     *   Z = SNES vertical (0..223, flipped so top of screen = far)
     *   Y = height (0 = table surface, walls extrude upward)
     *
     * Camera looks down at the "table" from a raised angle.
     */
    cam->target_x = 128.0f;  /* center of SNES screen */
    cam->target_y = 2.0f;    /* slightly above ground */
    cam->target_z = 112.0f;  /* center of SNES screen Y */

    cam->distance = 300.0f;
    cam->yaw = 0.0f;         /* camera at +Z looking toward -Z (SNES bottom=near, top=far) */
    cam->pitch = 50.0f;      /* nice elevated angle looking down at diorama */

    cam->aspect = aspect;
    cam->fov = 45.0f;
    cam->near_plane = 1.0f;
    cam->far_plane = 2000.0f;

    camera_update(cam);
}

void camera_update(Camera *cam) {
    /* Clamp pitch */
    if (cam->pitch > 89.0f) cam->pitch = 89.0f;
    if (cam->pitch < 5.0f) cam->pitch = 5.0f;
    if (cam->distance < 10.0f) cam->distance = 10.0f;
    if (cam->distance > 1500.0f) cam->distance = 1500.0f;

    /* Compute eye position from spherical coordinates */
    float yaw_rad = deg2rad(cam->yaw);
    float pitch_rad = deg2rad(cam->pitch);

    cam->eye_x = cam->target_x + cam->distance * cosf(pitch_rad) * sinf(yaw_rad);
    cam->eye_y = cam->target_y + cam->distance * sinf(pitch_rad);
    cam->eye_z = cam->target_z + cam->distance * cosf(pitch_rad) * cosf(yaw_rad);

    /* Build matrices */
    mat4_lookat(cam->view,
                cam->eye_x, cam->eye_y, cam->eye_z,
                cam->target_x, cam->target_y, cam->target_z,
                0, 1, 0);
    mat4_perspective(cam->proj, cam->fov, cam->aspect,
                     cam->near_plane, cam->far_plane);
}

void camera_orbit(Camera *cam, float dyaw, float dpitch) {
    cam->yaw += dyaw;
    cam->pitch += dpitch;
    camera_update(cam);
}

void camera_zoom(Camera *cam, float delta) {
    cam->distance -= delta * 10.0f;
    camera_update(cam);
}

void camera_pan(Camera *cam, float dx, float dy) {
    /* Pan relative to view direction */
    float yaw_rad = deg2rad(cam->yaw);
    float sx = cosf(yaw_rad);
    float sz = -sinf(yaw_rad);

    cam->target_x += sx * dx - sz * dy;
    cam->target_z += sz * dx + sx * dy;
    camera_update(cam);
}

void camera_set_topdown(Camera *cam) {
    cam->yaw = 0.0f;
    cam->pitch = 89.0f;
    cam->distance = 260.0f;
    cam->target_x = 128.0f;
    cam->target_y = 0.0f;
    cam->target_z = 112.0f;
    camera_update(cam);
}

void camera_set_isometric(Camera *cam) {
    cam->yaw = -20.0f;       /* slightly rotated for nice isometric angle */
    cam->pitch = 50.0f;
    cam->distance = 300.0f;
    cam->target_x = 128.0f;
    cam->target_y = 2.0f;
    cam->target_z = 112.0f;
    camera_update(cam);
}

void camera_set_side(Camera *cam) {
    cam->yaw = 0.0f;
    cam->pitch = 15.0f;
    cam->distance = 350.0f;
    cam->target_x = 128.0f;
    cam->target_y = 2.0f;
    cam->target_z = 112.0f;
    camera_update(cam);
}
