/*
 * menu.cpp — ImGui menu system for 3dSNES.
 */

#include "3dsnes/menu.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

/* ── SNES Button Names ──────────────────────────────────────────── */

enum {
    SNES_UP = 0, SNES_DOWN, SNES_LEFT, SNES_RIGHT,
    SNES_B, SNES_A, SNES_Y, SNES_X,
    SNES_L, SNES_R, SNES_SELECT, SNES_START,
    SNES_BTN_COUNT
};

static const char *snes_btn_names[SNES_BTN_COUNT] = {
    "Up", "Down", "Left", "Right",
    "B", "A", "Y", "X",
    "L", "R", "Select", "Start"
};

static SDL_Scancode g_p1_keys[SNES_BTN_COUNT] = {
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_A, SDL_SCANCODE_S,
    SDL_SCANCODE_D, SDL_SCANCODE_C, SDL_SCANCODE_TAB, SDL_SCANCODE_RETURN
};

static SDL_Scancode g_p2_keys[SNES_BTN_COUNT] = {
    SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_6,
    SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_9,
    SDL_SCANCODE_KP_MINUS, SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_ENTER
};

/* ── Menu State ─────────────────────────────────────────────────── */

static struct {
    bool     three_d_enabled;
    int      scale_factor;
    int      renderer_type;
    bool     show_fps;
    bool     vsync;

    bool     screenshot_requested;
    bool     quit_requested;
    int      view_preset;
    bool     scale_changed;

    bool     save_requested;
    bool     load_requested;

    bool     show_about;
    bool     show_controls;

    int      rebind_player;
    int      rebind_button;

    float    current_fps;
    int      voxel_count;

    char    *rom_path;

    /* Toast */
    char     toast_msg[256];
    Uint32   toast_start;  /* SDL_GetTicks when toast was triggered */
} g_menu;

static SDL_Renderer *g_renderer = NULL;
static SDL_Window   *g_window = NULL;

/* ── File Dialog ────────────────────────────────────────────────── */

static char *open_rom_dialog(void) {
#ifdef _WIN32
    char filename[MAX_PATH] = {0};
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "SNES ROMs\0*.sfc;*.smc;*.zip\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = "Load SNES ROM";
    if (GetOpenFileNameA(&ofn)) {
        char *result = (char *)malloc(strlen(filename) + 1);
        strcpy(result, filename);
        return result;
    }
#endif
    return NULL;
}

/* ── Theme ──────────────────────────────────────────────────────── */

static void apply_theme(void) {
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    colors[ImGuiCol_WindowBg]        = ImVec4(0.08f, 0.08f, 0.12f, 0.94f);
    colors[ImGuiCol_MenuBarBg]       = ImVec4(0.10f, 0.10f, 0.16f, 1.00f);
    colors[ImGuiCol_Header]          = ImVec4(0.15f, 0.30f, 0.55f, 0.80f);
    colors[ImGuiCol_HeaderHovered]   = ImVec4(0.20f, 0.40f, 0.65f, 0.80f);
    colors[ImGuiCol_HeaderActive]    = ImVec4(0.25f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_Button]          = ImVec4(0.15f, 0.30f, 0.55f, 0.65f);
    colors[ImGuiCol_ButtonHovered]   = ImVec4(0.20f, 0.40f, 0.65f, 0.80f);
    colors[ImGuiCol_ButtonActive]    = ImVec4(0.25f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_FrameBg]         = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]  = ImVec4(0.18f, 0.18f, 0.28f, 1.00f);
    colors[ImGuiCol_CheckMark]       = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]      = ImVec4(0.30f, 0.55f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]= ImVec4(0.40f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_PopupBg]         = ImVec4(0.08f, 0.08f, 0.12f, 0.96f);
    colors[ImGuiCol_TitleBg]         = ImVec4(0.08f, 0.08f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgActive]   = ImVec4(0.12f, 0.20f, 0.40f, 1.00f);

    style.WindowRounding = 4.0f;
    style.FrameRounding  = 3.0f;
    style.GrabRounding   = 3.0f;
    style.PopupRounding  = 4.0f;
}

/* ── About Window ───────────────────────────────────────────────── */

static void draw_about_window(void) {
    if (!g_menu.show_about) return;
    ImGui::SetNextWindowSize(ImVec2(380, 220), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(200, 150), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("About 3dSNES", &g_menu.show_about, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Spacing();
        ImGui::Text("3dSNES  v0.1");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("A free, open-source 3D voxel renderer");
        ImGui::Text("for Super Nintendo games.");
        ImGui::Spacing();
        ImGui::Text("Inspired by 3dSen (NES).");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Emulation:  LakeSnes");
        ImGui::TextDisabled("Graphics:   SDL2 + Software Rasterizer");
        ImGui::TextDisabled("UI:         Dear ImGui");
    }
    ImGui::End();
}

/* ── Controls Window ────────────────────────────────────────────── */

static const char *scancode_name(SDL_Scancode sc) {
    const char *name = SDL_GetScancodeName(sc);
    return (name && name[0]) ? name : "???";
}

static void draw_controls_window(void) {
    if (!g_menu.show_controls) return;
    ImGui::SetNextWindowSize(ImVec2(520, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Controls", &g_menu.show_controls)) {
        if (g_menu.rebind_button >= 0) {
            ImGui::OpenPopup("Press a key...");
        }
        if (ImGui::BeginPopupModal("Press a key...", NULL,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::Text("Press a key for P%d %s",
                        g_menu.rebind_player + 1,
                        snes_btn_names[g_menu.rebind_button]);
            ImGui::Text("(Press Escape to cancel)");
            const Uint8 *state = SDL_GetKeyboardState(NULL);
            for (int i = 0; i < SDL_NUM_SCANCODES; i++) {
                if (state[i]) {
                    if (i == SDL_SCANCODE_ESCAPE) {
                        g_menu.rebind_button = -1;
                        ImGui::CloseCurrentPopup();
                    } else {
                        SDL_Scancode *keys = (g_menu.rebind_player == 0) ? g_p1_keys : g_p2_keys;
                        keys[g_menu.rebind_button] = (SDL_Scancode)i;
                        g_menu.rebind_button = -1;
                        ImGui::CloseCurrentPopup();
                    }
                    break;
                }
            }
            ImGui::EndPopup();
        }

        for (int player = 0; player < 2; player++) {
            char label[32];
            snprintf(label, sizeof(label), "Player %d (Keyboard)", player + 1);
            if (ImGui::CollapsingHeader(label,
                    player == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                SDL_Scancode *keys = (player == 0) ? g_p1_keys : g_p2_keys;
                char tbl_id[8]; snprintf(tbl_id, sizeof(tbl_id), "p%d", player + 1);
                if (ImGui::BeginTable(tbl_id, 3,
                        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, 80);
                    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 120);
                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80);
                    ImGui::TableHeadersRow();
                    for (int b = 0; b < SNES_BTN_COUNT; b++) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::Text("%s", snes_btn_names[b]);
                        ImGui::TableNextColumn(); ImGui::Text("%s", scancode_name(keys[b]));
                        ImGui::TableNextColumn();
                        char bid[32]; snprintf(bid, sizeof(bid), "Rebind##%d_%d", player, b);
                        if (ImGui::SmallButton(bid)) {
                            g_menu.rebind_player = player;
                            g_menu.rebind_button = b;
                        }
                    }
                    ImGui::EndTable();
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Gamepad: Connect any SDL-compatible controller.");
        ImGui::Text("Player 1 uses first gamepad, Player 2 uses second.");
        ImGui::Spacing();
        if (ImGui::Button("Reset to Defaults")) {
            SDL_Scancode d1[] = { SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
                SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_A, SDL_SCANCODE_S,
                SDL_SCANCODE_D, SDL_SCANCODE_C, SDL_SCANCODE_TAB, SDL_SCANCODE_RETURN };
            SDL_Scancode d2[] = { SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_6,
                SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_9,
                SDL_SCANCODE_KP_MINUS, SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_ENTER };
            memcpy(g_p1_keys, d1, sizeof(d1));
            memcpy(g_p2_keys, d2, sizeof(d2));
        }
    }
    ImGui::End();
}

/* ── Menu Bar ───────────────────────────────────────────────────── */

static void draw_menu_bar(void) {
    g_menu.view_preset = 0;
    g_menu.scale_changed = false;

    if (ImGui::BeginMainMenuBar()) {
        /* ── File ──────────────────────────────────────────────── */
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Load ROM...", "Ctrl+O")) {
                char *path = open_rom_dialog();
                if (path) {
                    free(g_menu.rom_path);
                    g_menu.rom_path = path;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save State", "F5")) { g_menu.save_requested = true; }
            if (ImGui::MenuItem("Load State", "F7")) { g_menu.load_requested = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Screenshot", "F12")) { g_menu.screenshot_requested = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Esc")) { g_menu.quit_requested = true; }
            ImGui::EndMenu();
        }

        /* ── Graphics ──────────────────────────────────────────── */
        if (ImGui::BeginMenu("Graphics")) {
            if (ImGui::MenuItem("3D Mode", "F1", g_menu.three_d_enabled)) {
                g_menu.three_d_enabled = !g_menu.three_d_enabled;
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Scale Factor")) {
                /* Start at 2x — 1x is too small for the menu */
                const char *labels[] = { "2x (512x448)", "3x (768x672)", "4x (1024x896)", "5x (1280x1120)" };
                const int   values[] = { 2, 3, 4, 5 };
                for (int i = 0; i < 4; i++) {
                    if (ImGui::MenuItem(labels[i], NULL, g_menu.scale_factor == values[i])) {
                        g_menu.scale_factor = values[i];
                        g_menu.scale_changed = true;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Renderer")) {
                if (ImGui::MenuItem("CPU (Software)", NULL, g_menu.renderer_type == 0)) {
                    g_menu.renderer_type = 0;
                }
                ImGui::MenuItem("GPU (OpenGL) — coming soon", NULL, false, false);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("V-Sync", NULL, g_menu.vsync)) { g_menu.vsync = !g_menu.vsync; }
            if (ImGui::MenuItem("Show FPS", NULL, g_menu.show_fps)) { g_menu.show_fps = !g_menu.show_fps; }
            ImGui::EndMenu();
        }

        /* ── View ──────────────────────────────────────────────── */
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Top-Down", "1")) { g_menu.view_preset = 1; }
            if (ImGui::MenuItem("Isometric", "2")) { g_menu.view_preset = 2; }
            if (ImGui::MenuItem("Side", "3")) { g_menu.view_preset = 3; }
            ImGui::EndMenu();
        }

        /* ── Controls ──────────────────────────────────────────── */
        if (ImGui::BeginMenu("Controls")) {
            if (ImGui::MenuItem("Configure Controls...")) { g_menu.show_controls = true; }
            ImGui::EndMenu();
        }

        /* ── About ─────────────────────────────────────────────── */
        if (ImGui::BeginMenu("About")) {
            if (ImGui::MenuItem("About 3dSNES...")) { g_menu.show_about = true; }
            ImGui::EndMenu();
        }

        /* ── FPS ───────────────────────────────────────────────── */
        if (g_menu.show_fps) {
            char fps_text[64];
            if (g_menu.three_d_enabled)
                snprintf(fps_text, sizeof(fps_text), "%.0f FPS | %dK voxels",
                         g_menu.current_fps, g_menu.voxel_count / 1000);
            else
                snprintf(fps_text, sizeof(fps_text), "%.0f FPS", g_menu.current_fps);
            float text_w = ImGui::CalcTextSize(fps_text).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - text_w - 10);
            ImGui::Text("%s", fps_text);
        }

        ImGui::EndMainMenuBar();
    }
}

/* ── Toast Notification ──────────────────────────────────────────── */

static void draw_toast(void) {
    if (g_menu.toast_msg[0] == '\0') return;
    Uint32 elapsed = SDL_GetTicks() - g_menu.toast_start;
    if (elapsed > 2500) {
        g_menu.toast_msg[0] = '\0';
        return;
    }

    /* Fade out in last 500ms */
    float alpha = 1.0f;
    if (elapsed > 2000) alpha = 1.0f - (elapsed - 2000) / 500.0f;

    ImGuiIO &io = ImGui::GetIO();
    ImVec2 pos(io.DisplaySize.x - 20, io.DisplaySize.y - 40);
    ImGui::SetNextWindowPos(pos, 0, ImVec2(1.0f, 1.0f)); /* anchor bottom-right */
    ImGui::SetNextWindowBgAlpha(0.7f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
    if (ImGui::Begin("##toast", NULL,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, alpha));
        ImGui::Text("%s", g_menu.toast_msg);
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

/* ── Public API ─────────────────────────────────────────────────── */

extern "C" void menu_init(SDL_Window *window, SDL_Renderer *renderer) {
    g_window = window;
    g_renderer = renderer;
    memset(&g_menu, 0, sizeof(g_menu));
    g_menu.show_fps = true;
    g_menu.vsync = true;
    g_menu.rebind_button = -1;

    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    g_menu.scale_factor = (w + 127) / 256;
    if (g_menu.scale_factor < 2) g_menu.scale_factor = 2;
    if (g_menu.scale_factor > 5) g_menu.scale_factor = 5;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = NULL;
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    apply_theme();
}

extern "C" void menu_shutdown(void) {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    free(g_menu.rom_path);
}

extern "C" void menu_process_event(SDL_Event *event) {
    ImGui_ImplSDL2_ProcessEvent(event);
}

extern "C" void menu_draw(void) {
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    draw_menu_bar();
    draw_about_window();
    draw_controls_window();
    draw_toast();
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_renderer);
}

extern "C" bool menu_wants_input(void) {
    ImGuiIO &io = ImGui::GetIO();
    return io.WantCaptureKeyboard || io.WantCaptureMouse;
}

extern "C" bool menu_get_3d_enabled(void)         { return g_menu.three_d_enabled; }
extern "C" void menu_set_3d_enabled(bool e)       { g_menu.three_d_enabled = e; }
extern "C" int  menu_get_scale_factor(void)        { return g_menu.scale_factor; }
extern "C" int  menu_get_renderer_type(void)       { return g_menu.renderer_type; }
extern "C" bool menu_get_show_fps(void)            { return g_menu.show_fps; }
extern "C" bool menu_get_vsync(void)               { return g_menu.vsync; }
extern "C" bool menu_get_scale_changed(void)       { return g_menu.scale_changed; }
extern "C" int  menu_get_view_preset(void)         { return g_menu.view_preset; }
extern "C" bool menu_screenshot_requested(void)    { return g_menu.screenshot_requested; }
extern "C" void menu_clear_screenshot_request(void){ g_menu.screenshot_requested = false; }
extern "C" bool menu_quit_requested(void)          { return g_menu.quit_requested; }
extern "C" void menu_set_fps(float fps)            { g_menu.current_fps = fps; }
extern "C" void menu_set_voxel_count(int count)    { g_menu.voxel_count = count; }
extern "C" bool menu_save_requested(void)          { return g_menu.save_requested; }
extern "C" void menu_clear_save_request(void)      { g_menu.save_requested = false; }
extern "C" bool menu_load_requested(void)          { return g_menu.load_requested; }
extern "C" void menu_clear_load_request(void)      { g_menu.load_requested = false; }
extern "C" char *menu_get_rom_path(void)           { return g_menu.rom_path; }
extern "C" void menu_clear_rom_path(void)          { free(g_menu.rom_path); g_menu.rom_path = NULL; }
extern "C" const SDL_Scancode *menu_get_p1_keys(void) { return g_p1_keys; }
extern "C" const SDL_Scancode *menu_get_p2_keys(void) { return g_p2_keys; }
extern "C" void menu_show_toast(const char *msg) {
    snprintf(g_menu.toast_msg, sizeof(g_menu.toast_msg), "%s", msg);
    g_menu.toast_start = SDL_GetTicks();
}
