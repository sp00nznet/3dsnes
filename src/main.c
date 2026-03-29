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
 *   F4                 — Capture/release SNES mouse
 *   Escape             — Quit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <SDL.h>

#include "snes/snes.h"
#include "snes/ppu.h"
#include "snes/dsp.h"
#include "snes/input.h"
#include "zip/zip.h"

#include <glad/glad.h>

#include "3dsnes/profile_manager.h"
#include "3dsnes/ppu_extract.h"
#include "3dsnes/voxelizer.h"
#include "3dsnes/soft_renderer.h"
#include "3dsnes/renderer.h"
#include "3dsnes/camera.h"
#include "3dsnes/menu.h"
#include "3dsnes/lua_scripting.h"
#include "stb_image_write.h"

/* ── Globals ─────────────────────────────────────────────────────── */

static SDL_Window *g_window = NULL;
static SDL_Renderer *g_sdl_renderer = NULL;
static SDL_Texture *g_sdl_texture_3d = NULL;  /* RGBA for software renderer */
static SDL_Texture *g_sdl_texture_2d = NULL;  /* RGBX for SNES framebuffer */
static SDL_AudioDeviceID g_audio_dev = 0;

static Snes *g_snes = NULL;
static uint8_t g_pixel_buf[512 * 480 * 4]; /* must be 480 lines for ppu_putPixels */
static int16_t g_audio_buf[800 * 2]; /* 1 frame @ 48000 Hz / 60 fps, stereo */

static SoftRenderer g_soft_renderer;
static Renderer g_gpu_renderer;
static bool g_gpu_renderer_ready = false;
static SDL_Window *g_gl_window = NULL;    /* hidden window for GL context */
static SDL_GLContext g_gl_context = NULL;
static bool g_use_software = true;  /* default to software renderer */
static Camera g_camera;
static VoxelMesh g_voxel_mesh;
static VoxelProfile g_profile;
static ExtractedFrame g_extracted;

static LuaScripting g_lua;
static uint32_t g_lua_reload_timer = 0;  /* throttle hot-reload checks */

static bool g_running = true;
static bool g_rom_loaded = false;
static bool g_show_3d = false;
static bool g_show_overlay = false;
static bool g_mouse_dragging = false;
static bool g_mouse_panning = false;
static int g_mouse_last_x, g_mouse_last_y;
static bool g_save_requested = false;
static bool g_load_requested = false;

/* SNES Mouse state */
static bool g_snes_mouse_captured = false;  /* SDL relative mouse mode active */
static int  g_snes_mouse_prev_port = 0;     /* track port changes */
static uint8_t g_snes_mouse_buttons = 0;    /* current button state */
static char g_state_path[512] = {0};  /* path for save state file */
static char g_rom_path_current[512] = {0}; /* currently loaded ROM */
static char g_rom_internal_name[22] = {0}; /* SNES header internal name */
static uint16_t g_rom_checksum = 0;
static char g_profile_path[512] = {0};     /* active profile JSON path */

/* ── ROM Loading ─────────────────────────────────────────────────── */

static bool has_ext(const char *name, const char *ext) {
    int nlen = (int)strlen(name);
    int elen = (int)strlen(ext);
    if (nlen < elen) return false;
    for (int i = 0; i < elen; i++) {
        char c = name[nlen - elen + i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != ext[i]) return false;
    }
    return true;
}

static uint8_t *load_file(const char *path, int *size) {
    /* Handle ZIP files — extract first .sfc/.smc/.fig inside */
    if (has_ext(path, ".zip")) {
        struct zip_t *zip = zip_open(path, 0, 'r');
        if (!zip) {
            fprintf(stderr, "Failed to open ZIP: %s\n", path);
            menu_show_toast("Failed to open ZIP file");
            return NULL;
        }
        int entries = zip_total_entries(zip);
        uint8_t *data = NULL;
        for (int i = 0; i < entries; i++) {
            zip_entry_openbyindex(zip, i);
            const char *name = zip_entry_name(zip);
            if (has_ext(name, ".sfc") || has_ext(name, ".smc") || has_ext(name, ".fig")) {
                printf("Extracting \"%s\" from ZIP\n", name);
                size_t sz = 0;
                zip_entry_read(zip, (void **)&data, &sz);
                *size = (int)sz;
                zip_entry_close(zip);
                break;
            }
            zip_entry_close(zip);
        }
        zip_close(zip);
        if (!data) {
            fprintf(stderr, "No ROM found inside ZIP: %s\n", path);
            menu_show_toast("No .sfc/.smc ROM found inside ZIP");
        }
        return data;
    }

    /* Plain ROM file */
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open: %s\n", path);
        menu_show_toast("Failed to open ROM file");
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

/* Map menu SNES button index to LakeSnes button index */
static const int menu_to_lakesnes[12] = {
    /* SNES_UP=0 */ 4, /* SNES_DOWN=1 */ 5, /* SNES_LEFT=2 */ 6, /* SNES_RIGHT=3 */ 7,
    /* SNES_B=4 */  0, /* SNES_A=5 */    8, /* SNES_Y=6 */    1, /* SNES_X=7 */     9,
    /* SNES_L=8 */ 10, /* SNES_R=9 */   11, /* SNES_SEL=10 */ 2, /* SNES_START=11 */ 3
};

static void handle_key(SDL_Keycode key, bool pressed) {
    SDL_Scancode sc = SDL_GetScancodeFromKey(key);
    const SDL_Scancode *p1 = menu_get_p1_keys();
    const SDL_Scancode *p2 = menu_get_p2_keys();

    /* Check player 1 bindings */
    for (int i = 0; i < 12; i++) {
        if (sc == p1[i]) {
            static uint16_t btn_state_p1 = 0;
            int btn = menu_to_lakesnes[i];
            uint16_t mask = 1 << btn;
            bool cur = (btn_state_p1 & mask) != 0;
            if (pressed != cur) {
                if (pressed) btn_state_p1 |= mask; else btn_state_p1 &= ~mask;
                snes_setButtonState(g_snes, 1, btn, pressed);
            }
            return;
        }
    }

    /* Check player 2 bindings */
    for (int i = 0; i < 12; i++) {
        if (sc == p2[i]) {
            static uint16_t btn_state_p2 = 0;
            int btn = menu_to_lakesnes[i];
            uint16_t mask = 1 << btn;
            bool cur = (btn_state_p2 & mask) != 0;
            if (pressed != cur) {
                if (pressed) btn_state_p2 |= mask; else btn_state_p2 &= ~mask;
                snes_setButtonState(g_snes, 2, btn, pressed);
            }
            return;
        }
    }
}

/* ── Screenshot ──────────────────────────────────────────────────── */

static char g_rom_basename[256] = {0}; /* set from ROM path, used for test mode filenames */

static void take_screenshot(SDL_Renderer *renderer, SDL_Window *window) {
    static int shot_num = 0;
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);

    uint8_t *pixels = (uint8_t *)malloc(w * h * 4);
    if (!pixels) return;

    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGBA32, pixels, w * 4) == 0) {
        char filename[512];
        snprintf(filename, sizeof(filename), "screenshot_%04d.png", shot_num++);
        stbi_write_png(filename, w, h, 4, pixels, w * 4);
        printf("Screenshot saved: %s\n", filename);
        fflush(stdout);
        menu_show_toast(filename);
    }
    free(pixels);
}

static char g_test_output_dir[512] = {0}; /* set from argv[0] directory */

static void take_test_screenshot(SDL_Renderer *renderer, const char *suffix) {
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    uint8_t *pixels = (uint8_t *)malloc(w * h * 4);
    if (!pixels) return;
    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGBA32, pixels, w * 4) == 0) {
        char filename[1024];
        if (g_test_output_dir[0])
            snprintf(filename, sizeof(filename), "%s/%s_%s.png", g_test_output_dir, g_rom_basename, suffix);
        else
            snprintf(filename, sizeof(filename), "%s_%s.png", g_rom_basename, suffix);
        int ok = stbi_write_png(filename, w, h, 4, pixels, w * 4);
        if (ok)
            printf("Screenshot saved: %s\n", filename);
        else
            printf("Screenshot FAILED: %s\n", filename);
        fflush(stdout);
    }
    free(pixels);
}

/* ── Save State ──────────────────────────────────────────────────── */

static void do_save_state(void) {
    /* Allocate buffer large enough for any SNES state */
    uint8_t *buf = (uint8_t *)malloc(512 * 1024);
    if (!buf) return;

    int size = snes_saveState(g_snes, buf);
    if (size > 0) {
        FILE *f = fopen(g_state_path, "wb");
        if (f) {
            fwrite(buf, 1, size, f);
            fclose(f);
            printf("State saved: %s (%d bytes)\n", g_state_path, size);
            char msg[300]; snprintf(msg, sizeof(msg), "State saved: %s", g_state_path);
            menu_show_toast(msg);
        } else {
            printf("Failed to write state: %s\n", g_state_path);
            menu_show_toast("Failed to save state!");
        }
    }
    free(buf);
    fflush(stdout);
}

static void do_load_state(void) {
    FILE *f = fopen(g_state_path, "rb");
    if (!f) {
        printf("No state file: %s\n", g_state_path);
        fflush(stdout);
        return;
    }
    fseek(f, 0, SEEK_END);
    int size = (int)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(size);
    fread(buf, 1, size, f);
    fclose(f);

    if (snes_loadState(g_snes, buf, size)) {
        printf("State loaded: %s (%d bytes)\n", g_state_path, size);
        menu_show_toast("State loaded");
    } else {
        printf("Failed to load state: %s\n", g_state_path);
        menu_show_toast("Failed to load state!");
    }
    free(buf);
    fflush(stdout);
}

/* ── GPU Renderer ──────────────────────────────────────────────── */

static bool init_gpu_renderer(int w, int h) {
    if (g_gpu_renderer_ready) return true;

    /* Create a hidden window with an OpenGL 3.3 context for offscreen rendering */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    g_gl_window = SDL_CreateWindow(
        "3dSNES GL", 0, 0, 1, 1,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN
    );
    if (!g_gl_window) {
        fprintf(stderr, "GPU init failed: SDL_CreateWindow: %s\n", SDL_GetError());
        menu_show_toast("GPU init failed (no GL window)");
        return false;
    }

    g_gl_context = SDL_GL_CreateContext(g_gl_window);
    if (!g_gl_context) {
        fprintf(stderr, "GPU init failed: SDL_GL_CreateContext: %s\n", SDL_GetError());
        menu_show_toast("GPU init failed (no GL context)");
        SDL_DestroyWindow(g_gl_window);
        g_gl_window = NULL;
        return false;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "GPU init failed: gladLoadGLLoader\n");
        menu_show_toast("GPU init failed (GLAD)");
        SDL_GL_DeleteContext(g_gl_context);
        g_gl_context = NULL;
        SDL_DestroyWindow(g_gl_window);
        g_gl_window = NULL;
        return false;
    }

    printf("OpenGL: %s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    if (!renderer_init(&g_gpu_renderer, w, h)) {
        fprintf(stderr, "GPU init failed: renderer_init\n");
        menu_show_toast("GPU init failed (renderer)");
        SDL_GL_DeleteContext(g_gl_context);
        g_gl_context = NULL;
        SDL_DestroyWindow(g_gl_window);
        g_gl_window = NULL;
        return false;
    }

    g_gpu_renderer_ready = true;
    printf("GPU renderer initialized (%dx%d)\n", w, h);
    fflush(stdout);
    menu_show_toast("GPU renderer active");
    return true;
}

static void shutdown_gpu_renderer(void) {
    if (g_gpu_renderer_ready) {
        SDL_GL_MakeCurrent(g_gl_window, g_gl_context);
        renderer_shutdown(&g_gpu_renderer);
        g_gpu_renderer_ready = false;
    }
    if (g_gl_context) {
        SDL_GL_DeleteContext(g_gl_context);
        g_gl_context = NULL;
    }
    if (g_gl_window) {
        SDL_DestroyWindow(g_gl_window);
        g_gl_window = NULL;
    }
}

/* ── Audio Helpers ────────────────────────────────────────────────── */

static void apply_master_volume(int16_t *buf, int sample_count, float volume) {
    for (int i = 0; i < sample_count * 2; i++) { /* *2 for stereo */
        buf[i] = (int16_t)(buf[i] * volume);
    }
}

static void sync_audio_settings(Snes *snes) {
    float vol = menu_get_master_volume();
    (void)vol; /* master volume applied post-mix */
    for (int i = 0; i < 8; i++) {
        snes->apu->dsp->channelMuted[i] = menu_get_channel_muted(i);
    }
}

static void set_state_path_from_rom(const char *rom_path) {
    /* Create state filename from ROM name: "romname.state" */
    const char *slash = strrchr(rom_path, '/');
    if (!slash) slash = strrchr(rom_path, '\\');
    const char *name = slash ? slash + 1 : rom_path;

    snprintf(g_state_path, sizeof(g_state_path), "%s", name);
    /* Replace extension with .state */
    char *dot = strrchr(g_state_path, '.');
    if (dot) *dot = '\0';
    strcat(g_state_path, ".state");
}

static VoxelProfile get_hardcoded_profile(const char *path) {
    const char *rp = path;
    const char *slash = strrchr(rp, '/');
    if (!slash) slash = strrchr(rp, '\\');
    if (slash) rp = slash + 1;

    if (strstr(rp, "Mario World") || strstr(rp, "mario world") || strstr(rp, "MARIO WORLD"))
        return voxel_profile_smw();
    if (strstr(rp, "Zelda") || strstr(rp, "zelda") || strstr(rp, "ZELDA"))
        return voxel_profile_zelda_alttp();
    return voxel_profile_generic();
}

static void select_voxel_profile(const char *path, const uint8_t *rom_data, int rom_size) {
    /* Extract ROM identity */
    profile_read_rom_identity(rom_data, rom_size, g_rom_internal_name, &g_rom_checksum);
    printf("ROM identity: \"%s\" checksum=0x%04X\n", g_rom_internal_name, g_rom_checksum);

    /* Build profile path and try loading */
    profile_build_path(g_profile_path, sizeof(g_profile_path),
                       g_rom_internal_name, g_rom_checksum);

    if (profile_load_json(g_profile_path, &g_profile)) {
        printf("Profile loaded: %s\n", g_profile_path);
    } else {
        /* Fall back to hardcoded profile */
        g_profile = get_hardcoded_profile(path);
        printf("Profile: hardcoded fallback (%s)\n", g_profile_path);
    }

    /* Tell the menu about the profile for the scene editor */
    menu_set_profile(&g_profile, g_profile_path, g_rom_internal_name);

    /* Load Lua script if one exists alongside the profile JSON */
    {
        char lua_path[512];
        snprintf(lua_path, sizeof(lua_path), "%s", g_profile_path);
        /* Replace .json with .lua */
        char *dot = strrchr(lua_path, '.');
        if (dot) snprintf(dot, sizeof(lua_path) - (dot - lua_path), ".lua");
        lua_scripting_unload(&g_lua);
        if (lua_scripting_load(&g_lua, lua_path)) {
            g_lua.snes = g_snes;
            g_lua.profile = &g_profile;
            g_lua.camera = &g_camera;
            g_lua.frame = &g_extracted;
            g_lua.frame_count = 0;
            lua_scripting_call_start(&g_lua);
        }
    }

    fflush(stdout);
}

static bool load_new_rom(const char *path) {
    int rom_size = 0;
    uint8_t *rom_data = load_file(path, &rom_size);
    if (!rom_data) return false;

    snes_reset(g_snes, true);
    if (!snes_loadRom(g_snes, rom_data, rom_size)) {
        fprintf(stderr, "Failed to load ROM: %s\n", path);
        menu_show_toast("Failed to load ROM (invalid or unsupported)");
        free(rom_data);
        return false;
    }
    printf("ROM loaded: %s (%d bytes)\n", path, rom_size);
    {
        const char *s = strrchr(path, '/');
        if (!s) s = strrchr(path, '\\');
        char msg[300];
        snprintf(msg, sizeof(msg), "Loaded: %s", s ? s + 1 : path);
        menu_show_toast(msg);
    }

    snprintf(g_rom_path_current, sizeof(g_rom_path_current), "%s", path);
    set_state_path_from_rom(path);
    select_voxel_profile(path, rom_data, rom_size);
    free(rom_data);
    g_rom_loaded = true;

    /* Update window title */
    const char *slash = strrchr(path, '/');
    if (!slash) slash = strrchr(path, '\\');
    const char *name = slash ? slash + 1 : path;
    char title[256];
    snprintf(title, sizeof(title), "3dSNES — %s", name);
    SDL_SetWindowTitle(g_window, title);

    fflush(stdout);
    return true;
}

/* ── Event Processing ────────────────────────────────────────────── */

static void process_events(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        /* Pass all events to ImGui first */
        menu_process_event(&ev);
        bool imgui_wants = menu_wants_input();

        switch (ev.type) {
        case SDL_QUIT:
            g_running = false;
            break;

        case SDL_KEYDOWN:
            if (ev.key.repeat) break;
            /* Hotkeys always work (even when menu is active) */
            if (ev.key.keysym.sym == SDLK_ESCAPE) {
                g_running = false;
            } else if (ev.key.keysym.sym == SDLK_F1) {
                g_show_3d = !g_show_3d;
                menu_set_3d_enabled(g_show_3d);
            } else if (ev.key.keysym.sym == SDLK_F5) {
                g_save_requested = true;
            } else if (ev.key.keysym.sym == SDLK_F7) {
                g_load_requested = true;
            } else if (ev.key.keysym.sym == SDLK_F4) {
                if (menu_get_snes_mouse_enabled()) {
                    g_snes_mouse_captured = !g_snes_mouse_captured;
                    SDL_SetRelativeMouseMode(g_snes_mouse_captured ? SDL_TRUE : SDL_FALSE);
                }
            } else if (ev.key.keysym.sym == SDLK_F12) {
                take_screenshot(g_sdl_renderer, g_window);
            } else if (ev.key.keysym.sym == SDLK_1) {
                camera_set_topdown(&g_camera);
            } else if (ev.key.keysym.sym == SDLK_2) {
                camera_set_isometric(&g_camera);
            } else if (ev.key.keysym.sym == SDLK_3) {
                camera_set_side(&g_camera);
            }
            /* Game input only when ImGui doesn't want it */
            if (!imgui_wants)
                handle_key(ev.key.keysym.sym, true);
            break;

        case SDL_KEYUP:
            if (!imgui_wants)
                handle_key(ev.key.keysym.sym, false);
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (g_snes_mouse_captured) {
                if (ev.button.button == SDL_BUTTON_LEFT)
                    g_snes_mouse_buttons |= 1;
                else if (ev.button.button == SDL_BUTTON_RIGHT)
                    g_snes_mouse_buttons |= 2;
            } else if (!imgui_wants) {
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    g_mouse_dragging = true;
                    g_mouse_last_x = ev.button.x;
                    g_mouse_last_y = ev.button.y;
                } else if (ev.button.button == SDL_BUTTON_MIDDLE) {
                    g_mouse_panning = true;
                    g_mouse_last_x = ev.button.x;
                    g_mouse_last_y = ev.button.y;
                }
            }
            break;

        case SDL_MOUSEBUTTONUP:
            if (g_snes_mouse_captured) {
                if (ev.button.button == SDL_BUTTON_LEFT)
                    g_snes_mouse_buttons &= ~1;
                else if (ev.button.button == SDL_BUTTON_RIGHT)
                    g_snes_mouse_buttons &= ~2;
            }
            if (ev.button.button == SDL_BUTTON_LEFT) g_mouse_dragging = false;
            if (ev.button.button == SDL_BUTTON_MIDDLE) g_mouse_panning = false;
            break;

        case SDL_MOUSEMOTION:
            if (g_snes_mouse_captured) {
                int port = menu_get_snes_mouse_port();
                snes_setMouseState(g_snes, port,
                    (int16_t)ev.motion.xrel, (int16_t)ev.motion.yrel,
                    (g_snes_mouse_buttons & 1) != 0,
                    (g_snes_mouse_buttons & 2) != 0);
            } else {
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
            }
            break;

        case SDL_MOUSEWHEEL:
            camera_zoom(&g_camera, (float)ev.wheel.y);
            break;

        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                /* Camera aspect stays locked to render buffer, not window */
            }
            break;
        }
    }
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    const char *rom_path = (argc >= 2 && argv[1][0] != '-') ? argv[1] : NULL;
    bool test_mode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0) test_mode = true;
    }
    if (test_mode && !rom_path) {
        fprintf(stderr, "Usage: 3dsnes <rom.sfc> --test\n");
        return 1;
    }

    /* Set test output directory from executable path */
    if (test_mode) {
        snprintf(g_test_output_dir, sizeof(g_test_output_dir), "%s", argv[0]);
        char *last_sep = strrchr(g_test_output_dir, '/');
        if (!last_sep) last_sep = strrchr(g_test_output_dir, '\\');
        if (last_sep) *last_sep = '\0';
        else g_test_output_dir[0] = '\0';
    }

    /* ── Initialize SDL with OpenGL ──────────────────────────── */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int win_w = 1280, win_h = 960;
    g_window = SDL_CreateWindow(
        "3dSNES",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!g_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* Use SDL_Renderer for presentation (no GL context — avoids driver conflicts) */
    g_sdl_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_sdl_renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return 1;
    }
    {
        SDL_RendererInfo rinfo;
        if (SDL_GetRendererInfo(g_sdl_renderer, &rinfo) == 0) {
            printf("SDL_Renderer: %s\n", rinfo.name);
        }
    }
    fflush(stdout);
    /* 3D texture at half resolution for performance — SDL_RenderCopy upscales */
    /* 3D renders at SNES native resolution for correct aspect ratio, SDL upscales */
    int render_w = 256, render_h = 224;
    g_sdl_texture_3d = SDL_CreateTexture(g_sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, render_w, render_h);
    /* 2D texture at native SNES resolution — we extract the active 256x224 area */
    g_sdl_texture_2d = SDL_CreateTexture(g_sdl_renderer, SDL_PIXELFORMAT_RGBX8888, SDL_TEXTUREACCESS_STREAMING, 256, 224);

    /* Initialize ImGui menu system */
    menu_init(g_window, g_sdl_renderer);

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
    lua_scripting_init(&g_lua);

    snes_setPixelFormat(g_snes, pixelFormatRGBX);
    /* NOTE: do NOT call snes_setPixels here — call it per-frame after snes_runFrame */

    /* Load ROM (optional — if no ROM given, user can load via File menu) */
    if (rom_path) {
        int rom_size = 0;
        uint8_t *rom_data = load_file(rom_path, &rom_size);
        if (!rom_data) return 1;

        if (!snes_loadRom(g_snes, rom_data, rom_size)) {
            fprintf(stderr, "Failed to load ROM: %s\n", rom_path);
            free(rom_data);
            return 1;
        }
        printf("ROM loaded: %s (%d bytes)\n", rom_path, rom_size);
        snprintf(g_rom_path_current, sizeof(g_rom_path_current), "%s", rom_path);
        set_state_path_from_rom(rom_path);
        select_voxel_profile(rom_path, rom_data, rom_size);
        free(rom_data);
        g_rom_loaded = true;

        /* Set ROM basename for test mode screenshots (sanitized for filenames) */
        {
            const char *s = strrchr(rom_path, '/');
            if (!s) s = strrchr(rom_path, '\\');
            snprintf(g_rom_basename, sizeof(g_rom_basename), "%s", s ? s + 1 : rom_path);
            char *dot = strrchr(g_rom_basename, '.');
            if (dot) *dot = '\0';
            /* Replace unsafe chars with underscores */
            for (char *p = g_rom_basename; *p; p++) {
                if (*p == '!' || *p == '[' || *p == ']' || *p == '(' || *p == ')' ||
                    *p == '\'' || *p == '&' || *p == ' ')
                    *p = '_';
            }
        }

        /* Set window title from ROM name */
        {
            const char *slash = strrchr(rom_path, '/');
            if (!slash) slash = strrchr(rom_path, '\\');
            const char *name = slash ? slash + 1 : rom_path;
            char title[256];
            snprintf(title, sizeof(title), "3dSNES — %s", name);
            SDL_SetWindowTitle(g_window, title);
        }
    }

    /* ── Initialize 3D systems ───────────────────────────────── */
    /* GL renderer disabled — using software renderer + SDL_Renderer display path */
    if (!soft_renderer_init(&g_soft_renderer, render_w, render_h)) {
        fprintf(stderr, "Failed to initialize software renderer\n");
        return 1;
    }

    camera_init(&g_camera, (float)render_w / (float)render_h);
    /* Start with a nice centered diorama view */
    g_camera.yaw = 0.0f;
    g_camera.pitch = 70.0f;    /* high angle — mostly top-down diorama */
    g_camera.distance = 300.0f;
    g_camera.target_x = 128.0f;
    g_camera.target_y = 2.0f;
    g_camera.target_z = 112.0f;
    camera_update(&g_camera);

    voxel_mesh_init(&g_voxel_mesh, 2000000); /* pre-allocate large, never realloc */

    /* Profile was already selected during ROM load above; default for no-ROM */
    if (!rom_path) {
        g_profile = voxel_profile_generic();
    }

    printf("\n3dSNES ready!\n");
    printf("  F1: Toggle 3D/2D    F2: Toggle overlay    F3: Wireframe\n");
    printf("  1: Top-down    2: Isometric    3: Side view\n");
    printf("  Mouse: Orbit/Pan/Zoom    WASD/Arrows: D-pad\n");
    printf("  Z: A    X: B    Tab: Select    Enter: Start    Esc: Quit\n\n");
    fflush(stdout);

    /* ── Main loop ───────────────────────────────────────────── */
    int wantedSamples = 800;
    Uint32 frame_start = SDL_GetTicks();
    const float target_frame_ms = 1000.0f / 60.0f; /* 16.67ms per SNES frame */
    float emu_accumulator = 0.0f;

    while (g_running) {
        /* Process input */
        process_events();

        /* Lua hot-reload check (~1Hz) */
        {
            Uint32 now_ticks = SDL_GetTicks();
            if (g_lua.loaded && now_ticks - g_lua_reload_timer > 1000) {
                g_lua_reload_timer = now_ticks;
                lua_scripting_check_reload(&g_lua);
            }
        }

        /* SNES Mouse: sync device type with menu setting */
        {
            bool mouse_en = menu_get_snes_mouse_enabled();
            int  mouse_port = menu_get_snes_mouse_port();
            int  want_type = mouse_en ? 2 : 1; /* inputDeviceMouse or inputDeviceController */

            /* Release capture if mouse was disabled */
            if (!mouse_en && g_snes_mouse_captured) {
                g_snes_mouse_captured = false;
                SDL_SetRelativeMouseMode(SDL_FALSE);
            }

            /* Update device type on the correct port */
            if (mouse_en) {
                if (g_snes_mouse_prev_port && g_snes_mouse_prev_port != mouse_port) {
                    snes_setInputDevice(g_snes, g_snes_mouse_prev_port, 1); /* restore old port to controller */
                }
                snes_setInputDevice(g_snes, mouse_port, want_type);
            } else if (g_snes_mouse_prev_port) {
                snes_setInputDevice(g_snes, g_snes_mouse_prev_port, 1);
            }
            g_snes_mouse_prev_port = mouse_en ? mouse_port : 0;

            /* Update button state each frame (motion is sent per-event) */
            if (g_snes_mouse_captured) {
                snes_setMouseState(g_snes, mouse_port, 0, 0,
                    (g_snes_mouse_buttons & 1) != 0,
                    (g_snes_mouse_buttons & 2) != 0);
            }
        }

        /* Time-based emulation: run enough SNES frames to keep up with real time.
         * This decouples emulation speed from render speed — game runs at 60fps
         * even when 3D rendering is slower. */
        bool mode7_active = false;
        bool show_3d_this_frame = false;

        Uint32 now = SDL_GetTicks();
        float elapsed = (float)(now - frame_start);
        frame_start = now;
        if (elapsed > 100.0f) elapsed = 100.0f; /* cap to avoid spiral of death */

        if (g_rom_loaded) {
        sync_audio_settings(g_snes);
        float master_vol = menu_get_master_volume();
        emu_accumulator += elapsed;
        int emu_frames = 0;
        while (emu_accumulator >= target_frame_ms && emu_frames < 4) {
            snes_runFrame(g_snes);
            emu_accumulator -= target_frame_ms;
            emu_frames++;

            /* Audio for each emu frame */
            snes_setSamples(g_snes, g_audio_buf, wantedSamples);
            if (g_audio_dev > 0) {
                apply_master_volume(g_audio_buf, wantedSamples, master_vol);
                if (SDL_GetQueuedAudioSize(g_audio_dev) <= (uint32_t)(wantedSamples * 4 * 6)) {
                    SDL_QueueAudio(g_audio_dev, g_audio_buf, wantedSamples * 4);
                }
            }
        }

        if (emu_frames == 0) {
            /* Rendering faster than 60fps — run one frame anyway */
            snes_runFrame(g_snes);
            snes_setSamples(g_snes, g_audio_buf, wantedSamples);
            if (g_audio_dev > 0) {
                apply_master_volume(g_audio_buf, wantedSamples, master_vol);
                if (SDL_GetQueuedAudioSize(g_audio_dev) <= (uint32_t)(wantedSamples * 4 * 6)) {
                    SDL_QueueAudio(g_audio_dev, g_audio_buf, wantedSamples * 4);
                }
            }
        }

        /* Copy pixels for 2D display (from last emu frame) */
        snes_setPixels(g_snes, g_pixel_buf);

        /* Auto-fallback: Mode 7 can't be voxelized */
        mode7_active = (g_snes->ppu->mode == 7);
        show_3d_this_frame = g_show_3d && !mode7_active;

        /* Only extract/voxelize when in 3D mode and not Mode 7 */
        if (show_3d_this_frame) {
            ppu_extract_frame(g_snes->ppu, &g_extracted);

            /* Lua scripting: Update hook + override-aware voxelization */
            if (g_lua.loaded) {
                g_lua.frame = &g_extracted;
                g_lua.frame_count = g_snes->frames;
                lua_scripting_clear_overrides(&g_lua);
                lua_scripting_call_update(&g_lua);
                voxelize_frame_ex(&g_extracted, &g_profile, &g_voxel_mesh,
                                  menu_get_visible_layers(),
                                  g_lua.tile_overrides, g_lua.sprite_overrides);
                lua_scripting_call_late_update(&g_lua);
            } else {
                voxelize_frame(&g_extracted, &g_profile, &g_voxel_mesh, menu_get_visible_layers());
            }
            /* Update layer info for scene editor */
            {
                int bg_tiles[4] = {0}, bg_prio1[4] = {0};
                for (int i = 0; i < g_extracted.bg_tile_count; i++) {
                    int l = g_extracted.bg_tiles[i].bg_layer;
                    if (l >= 0 && l < 4) {
                        bg_tiles[l]++;
                        if (g_extracted.bg_tiles[i].priority) bg_prio1[l]++;
                    }
                }
                menu_set_layer_info(bg_tiles, bg_prio1, g_extracted.sprite_count);
            }
        }

        /* Set renderer clear color from BG1 (background layer) most common color */
        if (show_3d_this_frame && g_extracted.bg_enabled[1] && g_extracted.brightness > 0) {
            /* Sample BG1 pixels and find the most common color (quantized to 5-bit) */
            typedef struct { uint16_t key; int count; } ColorBucket;
            ColorBucket buckets[32]; /* small hash table */
            int nbuckets = 0;
            int best_count = 0;
            uint8_t best_r = 0, best_g = 0, best_b = 0;

            for (int i = 0; i < g_extracted.bg_tile_count; i++) {
                if (g_extracted.bg_tiles[i].bg_layer != 1) continue;
                /* Sample 4 pixels per tile (corners) for speed */
                for (int s = 0; s < 4; s++) {
                    int sr = (s & 2) ? 7 : 0;
                    int sc = (s & 1) ? 7 : 0;
                    const uint8_t *px = g_extracted.bg_tiles[i].decoded.pixels[sr][sc];
                    if (px[3] == 0 || px[0] + px[1] + px[2] < 12) continue;
                    /* Quantize to 5-bit per channel for bucketing */
                    uint16_t key = ((px[0] >> 3) << 10) | ((px[1] >> 3) << 5) | (px[2] >> 3);
                    /* Find or insert in bucket list */
                    int found = -1;
                    for (int b = 0; b < nbuckets; b++) {
                        if (buckets[b].key == key) { found = b; break; }
                    }
                    if (found >= 0) {
                        buckets[found].count++;
                        if (buckets[found].count > best_count) {
                            best_count = buckets[found].count;
                            best_r = px[0]; best_g = px[1]; best_b = px[2];
                        }
                    } else if (nbuckets < 32) {
                        buckets[nbuckets].key = key;
                        buckets[nbuckets].count = 1;
                        if (1 > best_count) {
                            best_count = 1;
                            best_r = px[0]; best_g = px[1]; best_b = px[2];
                        }
                        nbuckets++;
                    }
                }
            }

            if (best_count > 0) {
                float br = g_extracted.brightness / 15.0f;
                g_soft_renderer.clear_r = (uint8_t)(best_r * br);
                g_soft_renderer.clear_g = (uint8_t)(best_g * br);
                g_soft_renderer.clear_b = (uint8_t)(best_b * br);
                g_profile.sky_r = best_r;
                g_profile.sky_g = best_g;
                g_profile.sky_b = best_b;
            }
        }
        } /* end if (g_rom_loaded) — emulation + extraction */

        /* ── DIAGNOSTIC: one-time dump at frame 60 ────────────── */
        if (g_rom_loaded) {
            static int diag_frame = 0;
            diag_frame++;
            if (diag_frame == 300 || diag_frame == 600) {
                printf("\n=== DIAGNOSTIC DUMP (frame %d) ===\n", diag_frame);
                printf("PPU mode: %d, brightness: %d\n", g_extracted.mode, g_extracted.brightness);

                /* Per-layer tile count and non-transparent pixel count */
                int layer_tiles[4] = {0};
                int layer_opaque[4] = {0};
                for (int i = 0; i < g_extracted.bg_tile_count; i++) {
                    int l = g_extracted.bg_tiles[i].bg_layer;
                    if (l < 0 || l > 3) continue;
                    layer_tiles[l]++;
                    for (int r = 0; r < 8; r++)
                        for (int c = 0; c < 8; c++)
                            if (g_extracted.bg_tiles[i].decoded.pixels[r][c][3] > 0)
                                layer_opaque[l]++;
                }
                for (int l = 0; l < 4; l++) {
                    printf("BG%d: enabled=%d, tiles=%d, opaque_pixels=%d, scroll=(%d,%d)\n",
                           l, g_extracted.bg_enabled[l], layer_tiles[l], layer_opaque[l],
                           g_extracted.bg_hscroll[l], g_extracted.bg_vscroll[l]);
                }
                printf("Sprites: enabled=%d, count=%d\n",
                       g_extracted.sprites_enabled, g_extracted.sprite_count);
                printf("Total voxels: %d\n", g_voxel_mesh.count);

                /* Sample first COLORFUL (not black) pixel from each layer */
                for (int l = 0; l < 4; l++) {
                    int samples = 0;
                    for (int i = 0; i < g_extracted.bg_tile_count && samples < 3; i++) {
                        if (g_extracted.bg_tiles[i].bg_layer != l) continue;
                        const ExtractedBgTile *bt = &g_extracted.bg_tiles[i];
                        for (int r = 0; r < 8; r++) {
                            for (int c = 0; c < 8; c++) {
                                const uint8_t *px = bt->decoded.pixels[r][c];
                                if (px[3] > 0 && (px[0] + px[1] + px[2]) > 30) {
                                    printf("  BG%d: tile=%d pal=%d pos=(%d,%d) "
                                           "px[%d][%d]=RGBA(%d,%d,%d,%d)\n",
                                           l, bt->tile_num, bt->palette_num,
                                           bt->screen_x, bt->screen_y,
                                           r, c, px[0], px[1], px[2], px[3]);
                                    samples++;
                                    goto next_sample;
                                }
                            }
                        }
                        next_sample:;
                    }
                    if (samples == 0 && layer_tiles[l] > 0)
                        printf("  BG%d: %d tiles but NO colorful pixels found!\n", l, layer_tiles[l]);
                }
                /* Count black vs colorful opaque pixels on BG0 */
                {
                    int black = 0, colored = 0;
                    for (int i = 0; i < g_extracted.bg_tile_count; i++) {
                        if (g_extracted.bg_tiles[i].bg_layer != 0) continue;
                        for (int r = 0; r < 8; r++)
                            for (int c = 0; c < 8; c++) {
                                const uint8_t *px = g_extracted.bg_tiles[i].decoded.pixels[r][c];
                                if (px[3] == 0) continue;
                                if (px[0] + px[1] + px[2] < 12) black++;
                                else colored++;
                            }
                    }
                    printf("BG0 pixel breakdown: %d colored, %d black\n", colored, black);
                }

                /* Compare 2D pixel at (128,112) with extracted tile at same position */
                {
                    int sx = 128, sy = 112;
                    /* 2D pixel from PPU output (BGRX byte order on LE for pixelFormatRGBX) */
                    int ppu_idx = (sy * 2 + 16) * 512 * 4 + sx * 2 * 4;
                    printf("2D pixel at (%d,%d): BGRX(%d,%d,%d,%d)\n",
                           sx, sy,
                           g_pixel_buf[ppu_idx+0], g_pixel_buf[ppu_idx+1],
                           g_pixel_buf[ppu_idx+2], g_pixel_buf[ppu_idx+3]);
                }

                printf("=== END DIAGNOSTIC ===\n\n");
                fflush(stdout);
            }

        /* ── Test mode: multi-stage screenshot + diagnostics ───── */
        if (test_mode) {
            static Uint32 test_start = 0;
            static int test_stage = 0;
            static float test_fps_2d = 0, test_fps_3d = 0;
            static bool test_mode7_seen = false;
            if (test_start == 0) test_start = SDL_GetTicks();
            Uint32 elapsed = SDL_GetTicks() - test_start;
            if (mode7_active) test_mode7_seen = true;

            /* Stage 0: 8s — 2D boot screenshot */
            if (test_stage == 0 && elapsed >= 8000) {
                take_test_screenshot(g_sdl_renderer, "2d_01");
                test_stage = 1;
            }
            /* Stage 1: 15s — 2D title screenshot, switch to 3D */
            else if (test_stage == 1 && elapsed >= 15000) {
                take_test_screenshot(g_sdl_renderer, "2d_02");
                test_fps_2d = menu_get_show_fps() ? 60.0f : 60.0f; /* approximate */
                g_show_3d = true;
                menu_set_3d_enabled(true);
                test_stage = 2;
            }
            /* Stage 2: 18s — 3D screenshot #1 */
            else if (test_stage == 2 && elapsed >= 18000) {
                take_test_screenshot(g_sdl_renderer, "3d_03");
                test_stage = 3;
            }
            /* Stage 3: 25s — 3D screenshot #2 */
            else if (test_stage == 3 && elapsed >= 25000) {
                take_test_screenshot(g_sdl_renderer, "3d_04");
                test_stage = 4;
            }
            /* Stage 4: 35s — 3D screenshot #3 */
            else if (test_stage == 4 && elapsed >= 35000) {
                take_test_screenshot(g_sdl_renderer, "3d_05");
                test_fps_3d = g_voxel_mesh.count > 0 ? 30.0f : 60.0f; /* rough */
                g_show_3d = false;
                menu_set_3d_enabled(false);
                test_stage = 5;
            }
            /* Stage 5: 38s — 2D comparison screenshot */
            else if (test_stage == 5 && elapsed >= 38000) {
                take_test_screenshot(g_sdl_renderer, "2d_06");
                test_stage = 6;
            }
            /* Stage 6: 40s — dump diagnostics JSON and exit */
            else if (test_stage == 6 && elapsed >= 40000) {
                /* Extract diagnostic data */
                ppu_extract_frame(g_snes->ppu, &g_extracted);
                voxelize_frame(&g_extracted, &g_profile, &g_voxel_mesh, menu_get_visible_layers());

                int layer_tiles[4] = {0}, layer_opaque[4] = {0};
                for (int i = 0; i < g_extracted.bg_tile_count; i++) {
                    int l = g_extracted.bg_tiles[i].bg_layer;
                    if (l < 0 || l > 3) continue;
                    layer_tiles[l]++;
                    for (int r = 0; r < 8; r++)
                        for (int c = 0; c < 8; c++)
                            if (g_extracted.bg_tiles[i].decoded.pixels[r][c][3] > 0)
                                layer_opaque[l]++;
                }

                /* Write JSON diagnostic */
                char json_path[1024];
                if (g_test_output_dir[0])
                    snprintf(json_path, sizeof(json_path), "%s/%s_diag.json", g_test_output_dir, g_rom_basename);
                else
                    snprintf(json_path, sizeof(json_path), "%s_diag.json", g_rom_basename);

                FILE *jf = fopen(json_path, "w");
                if (jf) {
                    fprintf(jf, "{\n");
                    fprintf(jf, "  \"rom\": \"%s\",\n", g_rom_path_current);
                    fprintf(jf, "  \"ppu_mode\": %d,\n", g_extracted.mode);
                    fprintf(jf, "  \"brightness\": %d,\n", g_extracted.brightness);
                    fprintf(jf, "  \"bg_layers\": [\n");
                    for (int l = 0; l < 4; l++) {
                        fprintf(jf, "    {\"layer\": %d, \"enabled\": %s, \"tiles\": %d, \"opaque_pixels\": %d}%s\n",
                                l, g_extracted.bg_enabled[l] ? "true" : "false",
                                layer_tiles[l], layer_opaque[l], l < 3 ? "," : "");
                    }
                    fprintf(jf, "  ],\n");
                    fprintf(jf, "  \"sprites\": {\"enabled\": %s, \"count\": %d},\n",
                            g_extracted.sprites_enabled ? "true" : "false", g_extracted.sprite_count);
                    fprintf(jf, "  \"voxel_count\": %d,\n", g_voxel_mesh.count);
                    fprintf(jf, "  \"mode7_detected\": %s,\n", test_mode7_seen ? "true" : "false");
                    fprintf(jf, "  \"test_basename\": \"%s\"\n", g_rom_basename);
                    fprintf(jf, "}\n");
                    fclose(jf);
                    printf("Diagnostic saved: %s\n", json_path);
                }
                fflush(stdout);
                g_running = false;
            }
        }
        } /* end if (g_rom_loaded) — diagnostics + test mode */

        /* ── FPS tracking ──────────────────────────────────────── */
        {
            static Uint32 fps_last = 0;
            static int fps_frames = 0;
            fps_frames++;
            Uint32 now = SDL_GetTicks();
            if (now - fps_last >= 1000) {
                menu_set_fps((float)fps_frames * 1000.0f / (float)(now - fps_last));
                fps_frames = 0;
                fps_last = now;
            }
        }

        /* Sync state with menu */
        g_show_3d = menu_get_3d_enabled();
        menu_set_voxel_count(g_voxel_mesh.count);

        /* Handle menu view presets */
        switch (menu_get_view_preset()) {
            case 1: camera_set_topdown(&g_camera); break;
            case 2: camera_set_isometric(&g_camera); break;
            case 3: camera_set_side(&g_camera); break;
        }

        /* Handle scale change */
        if (menu_get_scale_changed()) {
            int s = menu_get_scale_factor();
            SDL_SetWindowSize(g_window, 256 * s, 224 * s);
            SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }

        /* Handle quit */
        if (menu_quit_requested()) {
            g_running = false;
        }

        /* Handle save/load state (from menu or hotkey) */
        if (g_rom_loaded && (menu_save_requested() || g_save_requested)) {
            do_save_state();
            menu_clear_save_request();
            g_save_requested = false;
        }
        if (g_rom_loaded && (menu_load_requested() || g_load_requested)) {
            do_load_state();
            menu_clear_load_request();
            g_load_requested = false;
        }

        /* Handle ROM loading from File -> Load ROM */
        {
            char *new_rom = menu_get_rom_path();
            if (new_rom) {
                load_new_rom(new_rom);
                menu_clear_rom_path();
            }
        }

        /* Auto-save profile when scene editor modifies it (1-second debounce) */
        if (g_rom_loaded && menu_profile_dirty()) {
            static Uint32 last_dirty = 0;
            Uint32 now_ms = SDL_GetTicks();
            if (last_dirty == 0) last_dirty = now_ms;
            if (now_ms - last_dirty >= 1000) {
                profile_save_json(g_profile_path, &g_profile,
                                  g_rom_internal_name, g_rom_checksum);
                menu_clear_profile_dirty();
                menu_show_toast("Profile saved");
                last_dirty = 0;
            }
        }

        /* Handle renderer type switch */
        {
            int rtype = menu_get_renderer_type();
            bool want_gpu = (rtype == 1);
            if (want_gpu != !g_use_software) {
                if (want_gpu) {
                    if (init_gpu_renderer(256, 224)) {
                        g_use_software = false;
                    } else {
                        /* Failed — revert menu to CPU */
                        g_use_software = true;
                    }
                } else {
                    g_use_software = true;
                    menu_show_toast("CPU renderer active");
                }
            }
        }

        /* Render and display */
        SDL_RenderClear(g_sdl_renderer);

        if (g_rom_loaded) {
        if (show_3d_this_frame) {
            /* Sync profile rendering settings to software renderer */
            soft_renderer_set_lighting(&g_soft_renderer,
                g_profile.light_dir[0], g_profile.light_dir[1], g_profile.light_dir[2],
                g_profile.ambient, g_profile.diffuse);
            g_soft_renderer.shadows_enabled = g_profile.shadows_enabled;
            g_soft_renderer.shadow_opacity = g_profile.shadow_opacity;
            g_soft_renderer.shadow_y = g_profile.shadow_y;
            g_soft_renderer.fxaa_enabled = menu_get_fxaa_enabled();
            g_soft_renderer.sky_type = g_profile.sky_type;
            g_soft_renderer.sky_top_r = g_profile.sky_top[0];
            g_soft_renderer.sky_top_g = g_profile.sky_top[1];
            g_soft_renderer.sky_top_b = g_profile.sky_top[2];
            g_soft_renderer.sky_bot_r = g_profile.sky_bot[0];
            g_soft_renderer.sky_bot_g = g_profile.sky_bot[1];
            g_soft_renderer.sky_bot_b = g_profile.sky_bot[2];

            if (g_use_software) {
                soft_renderer_draw(&g_soft_renderer, &g_camera,
                                   g_voxel_mesh.instances, g_voxel_mesh.count);
                const uint8_t *rendered = soft_renderer_pixels(&g_soft_renderer);
                SDL_UpdateTexture(g_sdl_texture_3d, NULL, rendered,
                                  g_soft_renderer.width * 4);
            } else if (g_gpu_renderer_ready) {
                SDL_GL_MakeCurrent(g_gl_window, g_gl_context);
                renderer_upload_voxels(&g_gpu_renderer, &g_voxel_mesh);
                /* Upload 2D framebuffer for overlay mode */
                renderer_upload_framebuffer(&g_gpu_renderer, g_pixel_buf, 512, 480);
                g_gpu_renderer.show_3d = true;
                g_gpu_renderer.show_overlay = g_show_overlay;
                /* Set clear color from sky */
                glClearColor(g_soft_renderer.clear_r / 255.0f,
                             g_soft_renderer.clear_g / 255.0f,
                             g_soft_renderer.clear_b / 255.0f, 1.0f);
                renderer_draw(&g_gpu_renderer, &g_camera, g_voxel_mesh.count);
                const uint8_t *rendered = renderer_readback(&g_gpu_renderer);
                SDL_UpdateTexture(g_sdl_texture_3d, NULL, rendered, 256 * 4);
            }
            SDL_RenderCopy(g_sdl_renderer, g_sdl_texture_3d, NULL, NULL);
        } else {
            /* 2D SNES framebuffer — extract 256x224 active area from 512x480 PPU output.
             * PPU buffer: 512px wide (doubled), 480 lines. Active starts at line 16.
             * Each pixel is doubled horizontally (512 → 256 real pixels). */
            static uint8_t active_buf[256 * 224 * 4];
            for (int y = 0; y < 224; y++) {
                const uint8_t *src = g_pixel_buf + (y * 2 + 16) * 512 * 4;
                uint8_t *dst = active_buf + y * 256 * 4;
                for (int x = 0; x < 256; x++) {
                    /* Take every other pixel (skip the doubled pixel) */
                    memcpy(dst + x * 4, src + x * 2 * 4, 4);
                }
            }
            SDL_UpdateTexture(g_sdl_texture_2d, NULL, active_buf, 256 * 4);
            SDL_RenderCopy(g_sdl_renderer, g_sdl_texture_2d, NULL, NULL);
        }
        } /* end if (g_rom_loaded) — game rendering */

        /* Draw ImGui menu bar on top of everything */
        menu_draw();

        SDL_RenderPresent(g_sdl_renderer);

        /* Screenshot after present so menu is captured too */
        if (menu_screenshot_requested()) {
            take_screenshot(g_sdl_renderer, g_window);
            menu_clear_screenshot_request();
        }

        if (!g_rom_loaded) SDL_Delay(16); /* ~60fps idle when no ROM */
    }

    /* ── Cleanup ─────────────────────────────────────────────── */
    menu_shutdown();
    voxel_mesh_free(&g_voxel_mesh);
    lua_scripting_shutdown(&g_lua);
    soft_renderer_shutdown(&g_soft_renderer);
    shutdown_gpu_renderer();
    snes_free(g_snes);

    if (g_audio_dev > 0) SDL_CloseAudioDevice(g_audio_dev);
    SDL_DestroyTexture(g_sdl_texture_3d);
    SDL_DestroyTexture(g_sdl_texture_2d);
    SDL_DestroyRenderer(g_sdl_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();

    return 0;
}
