/*
 * lua_scripting.h — Per-game Lua scripting for 3dSNES.
 *
 * Loads .lua scripts alongside profile JSONs. Scripts can read SNES RAM,
 * modify profile settings, adjust camera, and control per-tile/sprite
 * visibility and transforms via hook functions (Start, Update, LateUpdate, End).
 */

#ifndef LUA_SCRIPTING_H
#define LUA_SCRIPTING_H

#include <stdint.h>
#include <stdbool.h>

#include "3dsnes/voxelizer.h"
#include "3dsnes/ppu_extract.h"
#include "3dsnes/camera.h"

/* Forward-declare Lua state (avoids pulling in lua.h everywhere) */
typedef struct lua_State lua_State;

/* Forward-declare Snes */
typedef struct Snes Snes;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lua_State *L;
    bool loaded;

    /* Which hooks exist in the script */
    bool has_start;
    bool has_update;
    bool has_late_update;
    bool has_end;

    char script_path[512];
    uint64_t script_mtime;   /* for hot-reload detection */

    /* Pointers set each frame before hooks run */
    Snes *snes;
    VoxelProfile *profile;
    Camera *camera;
    ExtractedFrame *frame;
    uint32_t frame_count;

    /* Override tables (allocated once, cleared each frame) */
    TileOverride tile_overrides[MAX_BG_TILES];
    SpriteOverride sprite_overrides[MAX_SPRITES];

    /* Error state */
    char last_error[512];
    bool error_active;
} LuaScripting;

/* Lifecycle */
bool lua_scripting_init(LuaScripting *ls);
void lua_scripting_shutdown(LuaScripting *ls);
bool lua_scripting_load(LuaScripting *ls, const char *script_path);
void lua_scripting_unload(LuaScripting *ls);

/* Hook calls (called from main loop) */
void lua_scripting_call_start(LuaScripting *ls);
void lua_scripting_call_update(LuaScripting *ls);
void lua_scripting_call_late_update(LuaScripting *ls);
void lua_scripting_call_end(LuaScripting *ls);

/* Clear override tables (call before Update hook each frame) */
void lua_scripting_clear_overrides(LuaScripting *ls);

/* Hot-reload: returns true if script was reloaded */
bool lua_scripting_check_reload(LuaScripting *ls);

#ifdef __cplusplus
}
#endif

#endif /* LUA_SCRIPTING_H */
