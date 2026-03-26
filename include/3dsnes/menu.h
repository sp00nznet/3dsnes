/*
 * menu.h — ImGui menu system for 3dSNES.
 * C API wrapping C++ ImGui calls.
 */

#ifndef MENU_H
#define MENU_H

#include <SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle */
void menu_init(SDL_Window *window, SDL_Renderer *renderer);
void menu_shutdown(void);
void menu_process_event(SDL_Event *event);
void menu_draw(void);
bool menu_wants_input(void);

/* Graphics settings */
bool menu_get_3d_enabled(void);
void menu_set_3d_enabled(bool enabled);
int  menu_get_scale_factor(void);
int  menu_get_renderer_type(void);  /* 0=CPU, 1=GPU (not yet implemented) */
bool menu_get_show_fps(void);
bool menu_get_vsync(void);
bool menu_get_scale_changed(void);

/* View presets (returns 0=none, 1=topdown, 2=iso, 3=side) */
int  menu_get_view_preset(void);

/* Actions */
bool menu_screenshot_requested(void);
void menu_clear_screenshot_request(void);
bool menu_quit_requested(void);
bool menu_save_requested(void);
void menu_clear_save_request(void);
bool menu_load_requested(void);
void menu_clear_load_request(void);

/* ROM loading — returns path or NULL (caller must free) */
char *menu_get_rom_path(void);
void  menu_clear_rom_path(void);

/* Stats display */
void menu_set_fps(float fps);
void menu_set_voxel_count(int count);

/* Toast notification (bottom-right, fades after ~2 seconds) */
void menu_show_toast(const char *message);

/* Input bindings — returns array of 12 scancodes */
const SDL_Scancode *menu_get_p1_keys(void);
const SDL_Scancode *menu_get_p2_keys(void);

#ifdef __cplusplus
}
#endif

#endif /* MENU_H */
