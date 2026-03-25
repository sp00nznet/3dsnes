/*
 * camera.h — 3D camera with orbit controls for viewing the voxel scene.
 */

#ifndef CAMERA_H
#define CAMERA_H

#include <stdbool.h>

typedef struct {
    /* Orbit parameters */
    float target_x, target_y, target_z;  /* look-at point */
    float distance;                       /* distance from target */
    float yaw;                            /* horizontal angle (degrees) */
    float pitch;                          /* vertical angle (degrees) */

    /* Computed matrices (column-major for OpenGL) */
    float view[16];
    float proj[16];
    float eye_x, eye_y, eye_z;

    /* Viewport */
    float aspect;
    float fov;          /* vertical FOV in degrees */
    float near_plane;
    float far_plane;
} Camera;

/* Initialize camera with sensible defaults for SNES top-down view */
void camera_init(Camera *cam, float aspect);

/* Recompute view/projection matrices from current parameters */
void camera_update(Camera *cam);

/* Orbit controls */
void camera_orbit(Camera *cam, float dyaw, float dpitch);
void camera_zoom(Camera *cam, float delta);
void camera_pan(Camera *cam, float dx, float dy);

/* Preset views */
void camera_set_topdown(Camera *cam);    /* classic 2D-like top-down */
void camera_set_isometric(Camera *cam);  /* isometric angle */
void camera_set_side(Camera *cam);       /* side view */

#endif /* CAMERA_H */
