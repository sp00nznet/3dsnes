/*
 * Minimal emulator test — basically LakeSnes's own main loop.
 * Tests whether the ROM runs correctly without any 3D code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <glad/glad.h>
#include "snes/snes.h"
#include "snes/ppu.h"
#include "zip/zip.h"
#include "3dsnes/ppu_extract.h"
#include "3dsnes/voxelizer.h"
#include "3dsnes/renderer.h"
#include "3dsnes/camera.h"

static Snes *snes;
static ExtractedFrame g_extracted;
static VoxelMesh g_mesh;
static VoxelProfile g_profile;
static Renderer g_renderer;
static Camera g_camera;
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_AudioDeviceID audioDev;
static int16_t audioBuffer[800 * 2];

static void handleInput(SDL_Keycode key, int pressed) {
    int btn = -1;
    switch(key) {
        case SDLK_z: btn = 0; break;      // B
        case SDLK_a: btn = 1; break;      // Y
        case SDLK_RSHIFT: btn = 2; break; // Select
        case SDLK_RETURN: btn = 3; break; // Start
        case SDLK_UP: btn = 4; break;
        case SDLK_DOWN: btn = 5; break;
        case SDLK_LEFT: btn = 6; break;
        case SDLK_RIGHT: btn = 7; break;
        case SDLK_x: btn = 8; break;      // A
        case SDLK_s: btn = 9; break;      // X
        case SDLK_d: btn = 10; break;     // L
        case SDLK_c: btn = 11; break;     // R
    }
    if(btn >= 0) snes_setButtonState(snes, 1, btn, pressed);
}

int main(int argc, char *argv[]) {
    if(argc < 2) { printf("Usage: test_emu rom.sfc\n"); return 1; }

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

    /* TEST: Use OpenGL window like 3dsnes does */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    window = SDL_CreateWindow("3dSNES Test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 512, 480, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(0);

    /* Load OpenGL functions */
    gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);

    /* Init renderer + camera */
    renderer_init(&g_renderer, 512, 480);
    camera_init(&g_camera, 512.0f / 480.0f);

    /* Still use SDL_Renderer for display (on top of GL context) */
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBX8888, SDL_TEXTUREACCESS_STREAMING, 512, 480);

    SDL_AudioSpec want = {0};
    want.freq = 48000; want.format = AUDIO_S16; want.channels = 2; want.samples = 1024;
    audioDev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if(audioDev) SDL_PauseAudioDevice(audioDev, 0);

    snes = snes_init();

    int sz = 0;
    uint8_t *rom = NULL;
    const char *path = argv[1];
    int plen = strlen(path);
    if(plen > 4 && (_stricmp(path+plen-4, ".zip") == 0)) {
        struct zip_t *zip = zip_open(path, 0, 'r');
        int entries = zip_total_entries(zip);
        for(int i = 0; i < entries; i++) {
            zip_entry_openbyindex(zip, i);
            const char *name = zip_entry_name(zip);
            int nlen = strlen(name);
            if(nlen > 4 && (_stricmp(name+nlen-4, ".sfc") == 0 || _stricmp(name+nlen-4, ".smc") == 0)) {
                size_t s = 0;
                zip_entry_read(zip, (void**)&rom, &s);
                sz = (int)s;
                zip_entry_close(zip);
                break;
            }
            zip_entry_close(zip);
        }
        zip_close(zip);
    } else {
        FILE *f = fopen(path, "rb");
        fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
        rom = malloc(sz); fread(rom, 1, sz, f); fclose(f);
    }

    snes_setPixelFormat(snes, pixelFormatRGBX);
    if(!snes_loadRom(snes, rom, sz)) { printf("Failed\n"); return 1; }
    free(rom);
    printf("Loaded OK\n");

    voxel_mesh_init(&g_mesh, 1000000);
    g_profile = voxel_profile_zelda_alttp();

    int running = 1;
    while(running) {
        SDL_Event ev;
        while(SDL_PollEvent(&ev)) {
            if(ev.type == SDL_QUIT) running = 0;
            if(ev.type == SDL_KEYDOWN) {
                if(ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
                handleInput(ev.key.keysym.sym, 1);
            }
            if(ev.type == SDL_KEYUP) handleInput(ev.key.keysym.sym, 0);
        }

        snes_runFrame(snes);

        // extract + voxelize + render 3D
        ppu_extract_frame(snes->ppu, &g_extracted);
        voxelize_frame(&g_extracted, &g_profile, &g_mesh);
        renderer_upload_voxels(&g_renderer, &g_mesh);
        renderer_draw(&g_renderer, &g_camera, g_mesh.count);

        // audio
        int wantedSamples = 800;
        snes_setSamples(snes, audioBuffer, wantedSamples);
        if(audioDev && SDL_GetQueuedAudioSize(audioDev) <= wantedSamples * 4 * 6)
            SDL_QueueAudio(audioDev, audioBuffer, wantedSamples * 4);

        // video — use UpdateTexture (LockTexture broken on some NVIDIA drivers)
        static uint8_t pixbuf[512 * 480 * 4];
        snes_setPixels(snes, pixbuf);
        SDL_UpdateTexture(texture, NULL, pixbuf, 512 * 4);

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    snes_free(snes);
    if(audioDev) SDL_CloseAudioDevice(audioDev);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
