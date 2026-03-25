/*
 * 3dSNES — A free 3D voxel renderer for SNES games.
 *
 * Runs a real SNES emulator (LakeSnes) and converts the 2D tile/sprite
 * output into 3D voxel scenes in real-time.
 *
 * Inspired by 3dSen (NES) — this is the SNES equivalent, and it's free.
 *
 * Controls:
 *   WASD / Arrow keys  — D-pad
 *   Z / Return         — A button
 *   X / RShift         — B button
 *   A                  — X button
 *   S                  — Y button
 *   Q                  — L shoulder
 *   W                  — R shoulder
 *   Tab                — Select
 *   Return             — Start
 *
 *   Mouse drag         — Orbit camera
 *   Mouse wheel        — Zoom
 *   Middle drag        — Pan
 *   1                  — Top-down view
 *   2                  — Isometric view
 *   3                  — Side view
 *   F1                 — Toggle 3D/2D mode
 *   F2                 — Toggle 2D overlay
 *   F3                 — Toggle wireframe
 *   Escape             — Quit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <SDL.h>
#include <glad/glad.h>

#include "snes/snes.h"
#include "snes/ppu.h"
#include "snes/input.h"

#include "3dsnes/ppu_extract.h"
#include "3dsnes/voxelizer.h"
#include "3dsnes/renderer.h"
#include "3dsnes/soft_renderer.h"
#include "3dsnes/camera.h"

/* ── Globals ─────────────────────────────────────────────────────── */

static SDL_Window *g_window = NULL;
static SDL_GLContext g_gl_ctx = NULL;
static SDL_Renderer *g_sdl_renderer = NULL;
static SDL_Texture *g_sdl_texture_3d = NULL;  /* RGBA for software renderer */
static SDL_Texture *g_sdl_texture_2d = NULL;  /* RGBX for SNES framebuffer */
static SDL_AudioDeviceID g_audio_dev = 0;

static Snes *g_snes = NULL;
static uint8_t g_pixel_buf[512 * 480 * 4]; /* must be 480 lines for ppu_putPixels */
static int16_t g_audio_buf[800 * 2]; /* 1 frame @ 48000 Hz / 60 fps, stereo */

static Renderer g_renderer;
static SoftRenderer g_soft_renderer;
static bool g_use_software = true;  /* default to software renderer */
static Camera g_camera;
static VoxelMesh g_voxel_mesh;
static VoxelProfile g_profile;
static ExtractedFrame g_extracted;

static bool g_running = true;
static bool g_mouse_dragging = false;
static bool g_mouse_panning = false;
static int g_mouse_last_x, g_mouse_last_y;

/* ── ROM Loading ─────────────────────────────────────────────────── */

static uint8_t *load_file(const char *path, int *size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open: %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *size = (int)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*size);
    fread(data, 1, *size, f);
    fclose(f);
    return data;
}

/* ── Input Mapping ───────────────────────────────────────────────── */

static void handle_key(SDL_Keycode key, bool pressed) {
    /* SNES button indices (from LakeSnes input.h) */
    enum {
        BTN_B = 0, BTN_Y = 1, BTN_SELECT = 2, BTN_START = 3,
        BTN_UP = 4, BTN_DOWN = 5, BTN_LEFT = 6, BTN_RIGHT = 7,
        BTN_A = 8, BTN_X = 9, BTN_L = 10, BTN_R = 11
    };

    /* Match LakeSnes's exact key mapping */
    int btn = -1;
    switch (key) {
        case SDLK_UP:     btn = BTN_UP; break;
        case SDLK_DOWN:   btn = BTN_DOWN; break;
        case SDLK_LEFT:   btn = BTN_LEFT; break;
        case SDLK_RIGHT:  btn = BTN_RIGHT; break;
        case SDLK_z:      btn = BTN_B; break;       /* B (like LakeSnes) */
        case SDLK_x:      btn = BTN_A; break;       /* A (like LakeSnes) */
        case SDLK_a:      btn = BTN_Y; break;       /* Y (like LakeSnes) */
        case SDLK_s:      btn = BTN_X; break;       /* X (like LakeSnes) */
        case SDLK_d:      btn = BTN_L; break;       /* L (like LakeSnes) */
        case SDLK_c:      btn = BTN_R; break;       /* R (like LakeSnes) */
        case SDLK_TAB:    btn = BTN_SELECT; break;  /* Select */
        case SDLK_RETURN: btn = BTN_START; break;   /* Start */
        case SDLK_SPACE:  btn = BTN_A; break;       /* alternate A/jump */
        default: break;
    }
    if (btn >= 0) {
        /* Debounce: only send if state actually changed */
        static uint16_t btn_state = 0;
        uint16_t mask = 1 << btn;
        bool currently_pressed = (btn_state & mask) != 0;
        if (pressed != currently_pressed) {
            if (pressed) btn_state |= mask; else btn_state &= ~mask;
            snes_setButtonState(g_snes, 1, btn, pressed);
        }
    }
}

/* ── Event Processing ────────────────────────────────────────────── */

static void process_events(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            g_running = false;
            break;

        case SDL_KEYDOWN:
            if (ev.key.repeat) break; /* ignore key repeats */
            if (ev.key.keysym.sym == SDLK_ESCAPE) {
                g_running = false;
            } else if (ev.key.keysym.sym == SDLK_F1) {
                g_renderer.show_3d = !g_renderer.show_3d;
            } else if (ev.key.keysym.sym == SDLK_F2) {
                g_renderer.show_overlay = !g_renderer.show_overlay;
            } else if (ev.key.keysym.sym == SDLK_F3) {
                g_renderer.wireframe = !g_renderer.wireframe;
            } else if (ev.key.keysym.sym == SDLK_1) {
                camera_set_topdown(&g_camera);
            } else if (ev.key.keysym.sym == SDLK_2) {
                camera_set_isometric(&g_camera);
            } else if (ev.key.keysym.sym == SDLK_3) {
                camera_set_side(&g_camera);
            }
            handle_key(ev.key.keysym.sym, true);
            break;

        case SDL_KEYUP:
            handle_key(ev.key.keysym.sym, false);
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                g_mouse_dragging = true;
                g_mouse_last_x = ev.button.x;
                g_mouse_last_y = ev.button.y;
            } else if (ev.button.button == SDL_BUTTON_MIDDLE) {
                g_mouse_panning = true;
                g_mouse_last_x = ev.button.x;
                g_mouse_last_y = ev.button.y;
            }
            break;

        case SDL_MOUSEBUTTONUP:
            if (ev.button.button == SDL_BUTTON_LEFT) g_mouse_dragging = false;
            if (ev.button.button == SDL_BUTTON_MIDDLE) g_mouse_panning = false;
            break;

        case SDL_MOUSEMOTION:
            if (g_mouse_dragging) {
                float dx = (float)(ev.motion.x - g_mouse_last_x);
                float dy = (float)(ev.motion.y - g_mouse_last_y);
                camera_orbit(&g_camera, dx * 0.3f, dy * 0.3f);
                g_mouse_last_x = ev.motion.x;
                g_mouse_last_y = ev.motion.y;
            }
            if (g_mouse_panning) {
                float dx = (float)(ev.motion.x - g_mouse_last_x);
                float dy = (float)(ev.motion.y - g_mouse_last_y);
                camera_pan(&g_camera, -dx * 0.5f, dy * 0.5f);
                g_mouse_last_x = ev.motion.x;
                g_mouse_last_y = ev.motion.y;
            }
            break;

        case SDL_MOUSEWHEEL:
            camera_zoom(&g_camera, (float)ev.wheel.y);
            break;

        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                int w = ev.window.data1;
                int h = ev.window.data2;
                renderer_resize(&g_renderer, w, h);
                g_camera.aspect = (float)w / (float)h;
                camera_update(&g_camera);
            }
            break;
        }
    }
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: 3dsnes <rom.sfc>\n");
        return 1;
    }

    const char *rom_path = argv[1];

    /* ── Initialize SDL with OpenGL ──────────────────────────── */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    int win_w = 1280, win_h = 960;
    g_window = SDL_CreateWindow(
        "3dSNES — Zelda: A Link to the Past",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!g_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    g_gl_ctx = SDL_GL_CreateContext(g_window);
    if (!g_gl_ctx) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return 1;
    }

    /* Load OpenGL functions */
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "Failed to load OpenGL via glad\n");
        SDL_GL_DeleteContext(g_gl_ctx);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return 1;
    }

    printf("OpenGL %s, %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    SDL_GL_SetSwapInterval(0); /* no GL vsync — SDL_Renderer handles it */

    /* Create SDL_Renderer for presentation (this path works with emulation) */
    g_sdl_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    g_sdl_texture_3d = SDL_CreateTexture(g_sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, win_w, win_h);
    g_sdl_texture_2d = SDL_CreateTexture(g_sdl_renderer, SDL_PIXELFORMAT_RGBX8888, SDL_TEXTUREACCESS_STREAMING, 512, 480);

    /* ── Initialize audio ────────────────────────────────────── */
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = 48000;
    want.format = AUDIO_S16;
    want.channels = 2;
    want.samples = 1024;
    want.callback = NULL; /* push mode */

    g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_audio_dev > 0) {
        SDL_PauseAudioDevice(g_audio_dev, 0);
    }

    /* ── Initialize SNES emulator ────────────────────────────── */
    g_snes = snes_init();
    if (!g_snes) {
        fprintf(stderr, "Failed to create SNES instance\n");
        return 1;
    }

    snes_setPixelFormat(g_snes, pixelFormatRGBX);
    /* NOTE: do NOT call snes_setPixels here — call it per-frame after snes_runFrame */

    /* Load ROM */
    int rom_size = 0;
    uint8_t *rom_data = load_file(rom_path, &rom_size);
    if (!rom_data) return 1;

    if (!snes_loadRom(g_snes, rom_data, rom_size)) {
        fprintf(stderr, "Failed to load ROM: %s\n", rom_path);
        free(rom_data);
        return 1;
    }
    printf("ROM loaded: %s (%d bytes)\n", rom_path, rom_size);
    free(rom_data);

    /* ── Initialize 3D systems ───────────────────────────────── */
    if (!renderer_init(&g_renderer, win_w, win_h)) {
        fprintf(stderr, "Failed to initialize GL renderer\n");
    }
    if (!soft_renderer_init(&g_soft_renderer, win_w, win_h)) {
        fprintf(stderr, "Failed to initialize software renderer\n");
        return 1;
    }

    camera_init(&g_camera, (float)win_w / (float)win_h);
    /* Start with a nice centered diorama view */
    g_camera.yaw = 0.0f;
    g_camera.pitch = 70.0f;    /* high angle — mostly top-down diorama */
    g_camera.distance = 300.0f;
    g_camera.target_x = 128.0f;
    g_camera.target_y = 2.0f;
    g_camera.target_z = 112.0f;
    camera_update(&g_camera);

    voxel_mesh_init(&g_voxel_mesh, 2000000); /* pre-allocate large, never realloc */
    g_profile = voxel_profile_zelda_alttp();

    printf("\n3dSNES ready!\n");
    printf("  F1: Toggle 3D/2D    F2: Toggle overlay    F3: Wireframe\n");
    printf("  1: Top-down    2: Isometric    3: Side view\n");
    printf("  Mouse: Orbit/Pan/Zoom    WASD/Arrows: D-pad\n");
    printf("  Z: A    X: B    Tab: Select    Enter: Start    Esc: Quit\n\n");

    /* ── Main loop ───────────────────────────────────────────── */
    int wantedSamples = 800;

    while (g_running) {
        /* Process input */
        process_events();

        /* Run one SNES frame */
        snes_runFrame(g_snes);

        /* Audio */
        snes_setSamples(g_snes, g_audio_buf, wantedSamples);
        if (g_audio_dev > 0) {
            if (SDL_GetQueuedAudioSize(g_audio_dev) <= (uint32_t)(wantedSamples * 4 * 6)) {
                SDL_QueueAudio(g_audio_dev, g_audio_buf, wantedSamples * 4);
            }
        }

        /* Copy pixels for 2D overlay */
        snes_setPixels(g_snes, g_pixel_buf);

        /* Extract PPU state and voxelize (once per render, not per emu frame) */
        ppu_extract_frame(g_snes->ppu, &g_extracted);
        voxelize_frame(&g_extracted, &g_profile, &g_voxel_mesh);

        /* Render and display */
        SDL_RenderClear(g_sdl_renderer);

        if (g_renderer.show_3d) {
            /* Software 3D renderer */
            static int dbg = 0;
            if (++dbg == 60) { dbg = 0; printf("SW render: %d voxels\n", g_voxel_mesh.count); }
            soft_renderer_draw(&g_soft_renderer, &g_camera,
                               g_voxel_mesh.instances, g_voxel_mesh.count);
            const uint8_t *rendered = soft_renderer_pixels(&g_soft_renderer);
            SDL_UpdateTexture(g_sdl_texture_3d, NULL, rendered,
                              g_soft_renderer.width * 4);
            SDL_RenderCopy(g_sdl_renderer, g_sdl_texture_3d, NULL, NULL);
        } else {
            /* 2D SNES framebuffer */
            void *tex_pixels; int tex_pitch;
            SDL_LockTexture(g_sdl_texture_2d, NULL, &tex_pixels, &tex_pitch);
            snes_setPixels(g_snes, (uint8_t*)tex_pixels);
            SDL_UnlockTexture(g_sdl_texture_2d);
            SDL_RenderCopy(g_sdl_renderer, g_sdl_texture_2d, NULL, NULL);
        }

        SDL_RenderPresent(g_sdl_renderer);
    }

    /* ── Cleanup ─────────────────────────────────────────────── */
    voxel_mesh_free(&g_voxel_mesh);
    soft_renderer_shutdown(&g_soft_renderer);
    renderer_shutdown(&g_renderer);
    snes_free(g_snes);

    if (g_audio_dev > 0) SDL_CloseAudioDevice(g_audio_dev);
    SDL_DestroyTexture(g_sdl_texture_3d);
    SDL_DestroyTexture(g_sdl_texture_2d);
    SDL_DestroyRenderer(g_sdl_renderer);
    SDL_GL_DeleteContext(g_gl_ctx);
    SDL_DestroyWindow(g_window);
    SDL_Quit();

    return 0;
}
