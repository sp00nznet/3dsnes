/*
 * lua_scripting.c — Per-game Lua scripting for 3dSNES.
 */

#include "3dsnes/lua_scripting.h"
#include "3dsnes/menu.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#include "snes/snes.h"

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/* Store the LuaScripting pointer in the Lua registry for C callbacks */
static LuaScripting *get_ls(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "_3dsnes_ls");
    LuaScripting *ls = (LuaScripting *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return ls;
}

static void set_error(LuaScripting *ls, const char *msg) {
    snprintf(ls->last_error, sizeof(ls->last_error), "%s", msg);
    ls->error_active = true;
    fprintf(stderr, "[lua] %s\n", msg);
    menu_show_toast(msg);
}

static bool safe_call(LuaScripting *ls, int nargs, int nresults) {
    if (lua_pcall(ls->L, nargs, nresults, 0) != LUA_OK) {
        const char *err = lua_tostring(ls->L, -1);
        set_error(ls, err ? err : "unknown Lua error");
        lua_pop(ls->L, 1);
        return false;
    }
    return true;
}

/* Instruction count hook to prevent infinite loops */
static void instruction_hook(lua_State *L, lua_Debug *ar) {
    (void)ar;
    luaL_error(L, "script exceeded instruction limit (infinite loop?)");
}

static uint64_t get_file_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (uint64_t)st.st_mtime;
}

/* ------------------------------------------------------------------ */
/*  Lua API: snes.*                                                    */
/* ------------------------------------------------------------------ */

static int l_snes_read(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    if (!ls->snes) return luaL_error(L, "SNES not available");
    int addr = (int)luaL_checkinteger(L, 1) & 0x1FFFF;
    lua_pushinteger(L, ls->snes->ram[addr]);
    return 1;
}

static int l_snes_read16(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    if (!ls->snes) return luaL_error(L, "SNES not available");
    int addr = (int)luaL_checkinteger(L, 1) & 0x1FFFF;
    uint16_t val = ls->snes->ram[addr] | (ls->snes->ram[(addr + 1) & 0x1FFFF] << 8);
    lua_pushinteger(L, val);
    return 1;
}

static int l_snes_readSigned(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    if (!ls->snes) return luaL_error(L, "SNES not available");
    int addr = (int)luaL_checkinteger(L, 1) & 0x1FFFF;
    int8_t val = (int8_t)ls->snes->ram[addr];
    lua_pushinteger(L, val);
    return 1;
}

static int l_snes_readSigned16(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    if (!ls->snes) return luaL_error(L, "SNES not available");
    int addr = (int)luaL_checkinteger(L, 1) & 0x1FFFF;
    int16_t val = (int16_t)(ls->snes->ram[addr] | (ls->snes->ram[(addr + 1) & 0x1FFFF] << 8));
    lua_pushinteger(L, val);
    return 1;
}

static const luaL_Reg snes_lib[] = {
    {"read", l_snes_read},
    {"read16", l_snes_read16},
    {"readSigned", l_snes_readSigned},
    {"readSigned16", l_snes_readSigned16},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/*  Lua API: profile.*                                                 */
/* ------------------------------------------------------------------ */

static int l_profile_getBgZ(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    int layer = (int)luaL_checkinteger(L, 1);
    if (layer < 0 || layer > 3) return luaL_error(L, "layer must be 0-3");
    lua_pushnumber(L, ls->profile->bg_z[layer]);
    return 1;
}

static int l_profile_setBgZ(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    int layer = (int)luaL_checkinteger(L, 1);
    if (layer < 0 || layer > 3) return luaL_error(L, "layer must be 0-3");
    ls->profile->bg_z[layer] = (float)luaL_checknumber(L, 2);
    return 0;
}

static int l_profile_getBgDepth(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    int layer = (int)luaL_checkinteger(L, 1);
    if (layer < 0 || layer > 3) return luaL_error(L, "layer must be 0-3");
    lua_pushnumber(L, ls->profile->bg_depth[layer]);
    return 1;
}

static int l_profile_setBgDepth(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    int layer = (int)luaL_checkinteger(L, 1);
    if (layer < 0 || layer > 3) return luaL_error(L, "layer must be 0-3");
    ls->profile->bg_depth[layer] = (float)luaL_checknumber(L, 2);
    return 0;
}

static int l_profile_setSpriteZ(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    ls->profile->sprite_z = (float)luaL_checknumber(L, 1);
    return 0;
}

static int l_profile_setSpriteDepth(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    ls->profile->sprite_depth = (float)luaL_checknumber(L, 1);
    return 0;
}

static int l_profile_setLayerAlpha(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    int layer = (int)luaL_checkinteger(L, 1);
    if (layer < 0 || layer > 3) return luaL_error(L, "layer must be 0-3");
    ls->profile->layer_alpha[layer] = (float)luaL_checknumber(L, 2);
    return 0;
}

static int l_profile_setSpriteAlpha(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    ls->profile->sprite_alpha = (float)luaL_checknumber(L, 1);
    return 0;
}

static int l_profile_setLightDir(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    ls->profile->light_dir[0] = (float)luaL_checknumber(L, 1);
    ls->profile->light_dir[1] = (float)luaL_checknumber(L, 2);
    ls->profile->light_dir[2] = (float)luaL_checknumber(L, 3);
    return 0;
}

static int l_profile_setAmbient(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    ls->profile->ambient = (float)luaL_checknumber(L, 1);
    return 0;
}

static int l_profile_setDiffuse(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    ls->profile->diffuse = (float)luaL_checknumber(L, 1);
    return 0;
}

static int l_profile_setShadows(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    ls->profile->shadows_enabled = lua_toboolean(L, 1);
    return 0;
}

static int l_profile_setShadowOpacity(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    ls->profile->shadow_opacity = (float)luaL_checknumber(L, 1);
    return 0;
}

static const luaL_Reg profile_lib[] = {
    {"getBgZ", l_profile_getBgZ},
    {"setBgZ", l_profile_setBgZ},
    {"getBgDepth", l_profile_getBgDepth},
    {"setBgDepth", l_profile_setBgDepth},
    {"setSpriteZ", l_profile_setSpriteZ},
    {"setSpriteDepth", l_profile_setSpriteDepth},
    {"setLayerAlpha", l_profile_setLayerAlpha},
    {"setSpriteAlpha", l_profile_setSpriteAlpha},
    {"setLightDir", l_profile_setLightDir},
    {"setAmbient", l_profile_setAmbient},
    {"setDiffuse", l_profile_setDiffuse},
    {"setShadows", l_profile_setShadows},
    {"setShadowOpacity", l_profile_setShadowOpacity},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/*  Lua API: camera.*                                                  */
/* ------------------------------------------------------------------ */

static int l_camera_setTarget(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    ls->camera->target_x = (float)luaL_checknumber(L, 1);
    ls->camera->target_y = (float)luaL_checknumber(L, 2);
    ls->camera->target_z = (float)luaL_checknumber(L, 3);
    camera_update(ls->camera);
    return 0;
}

static int l_camera_getTarget(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    lua_pushnumber(L, ls->camera->target_x);
    lua_pushnumber(L, ls->camera->target_y);
    lua_pushnumber(L, ls->camera->target_z);
    return 3;
}

static int l_camera_setDistance(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    ls->camera->distance = (float)luaL_checknumber(L, 1);
    camera_update(ls->camera);
    return 0;
}

static int l_camera_getDistance(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    lua_pushnumber(L, ls->camera->distance);
    return 1;
}

static int l_camera_setYaw(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    ls->camera->yaw = (float)luaL_checknumber(L, 1);
    camera_update(ls->camera);
    return 0;
}

static int l_camera_getYaw(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    lua_pushnumber(L, ls->camera->yaw);
    return 1;
}

static int l_camera_setPitch(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    ls->camera->pitch = (float)luaL_checknumber(L, 1);
    camera_update(ls->camera);
    return 0;
}

static int l_camera_getPitch(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    lua_pushnumber(L, ls->camera->pitch);
    return 1;
}

static int l_camera_setFov(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    ls->camera->fov = (float)luaL_checknumber(L, 1);
    camera_update(ls->camera);
    return 0;
}

static const luaL_Reg camera_lib[] = {
    {"setTarget", l_camera_setTarget},
    {"getTarget", l_camera_getTarget},
    {"setDistance", l_camera_setDistance},
    {"getDistance", l_camera_getDistance},
    {"setYaw", l_camera_setYaw},
    {"getYaw", l_camera_getYaw},
    {"setPitch", l_camera_setPitch},
    {"getPitch", l_camera_getPitch},
    {"setFov", l_camera_setFov},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/*  Lua API: tile.*                                                    */
/* ------------------------------------------------------------------ */

static int l_tile_count(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    lua_pushinteger(L, ls->frame ? ls->frame->bg_tile_count : 0);
    return 1;
}

static int l_tile_getInfo(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (!ls->frame || idx < 0 || idx >= ls->frame->bg_tile_count)
        return luaL_error(L, "tile index %d out of range", idx);

    const ExtractedBgTile *t = &ls->frame->bg_tiles[idx];
    lua_createtable(L, 0, 7);
    lua_pushinteger(L, t->bg_layer);   lua_setfield(L, -2, "layer");
    lua_pushinteger(L, t->screen_x);   lua_setfield(L, -2, "screen_x");
    lua_pushinteger(L, t->screen_y);   lua_setfield(L, -2, "screen_y");
    lua_pushinteger(L, t->tile_num);   lua_setfield(L, -2, "tile_num");
    lua_pushinteger(L, t->palette_num);lua_setfield(L, -2, "palette_num");
    lua_pushinteger(L, t->priority);   lua_setfield(L, -2, "priority");
    return 1;
}

static int l_tile_setOffset(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= MAX_BG_TILES) return luaL_error(L, "tile index out of range");
    ls->tile_overrides[idx].offset_x = (float)luaL_checknumber(L, 2);
    ls->tile_overrides[idx].offset_y = (float)luaL_checknumber(L, 3);
    ls->tile_overrides[idx].offset_z = (float)luaL_checknumber(L, 4);
    ls->tile_overrides[idx].dirty = true;
    return 0;
}

static int l_tile_setAlpha(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= MAX_BG_TILES) return luaL_error(L, "tile index out of range");
    ls->tile_overrides[idx].alpha_mul = (float)luaL_checknumber(L, 2);
    ls->tile_overrides[idx].dirty = true;
    return 0;
}

static int l_tile_setHidden(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= MAX_BG_TILES) return luaL_error(L, "tile index out of range");
    ls->tile_overrides[idx].hidden = lua_toboolean(L, 2);
    ls->tile_overrides[idx].dirty = true;
    return 0;
}

static const luaL_Reg tile_lib[] = {
    {"count", l_tile_count},
    {"getInfo", l_tile_getInfo},
    {"setOffset", l_tile_setOffset},
    {"setAlpha", l_tile_setAlpha},
    {"setHidden", l_tile_setHidden},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/*  Lua API: sprite.*                                                  */
/* ------------------------------------------------------------------ */

static int l_sprite_count(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    lua_pushinteger(L, ls->frame ? ls->frame->sprite_count : 0);
    return 1;
}

static int l_sprite_getInfo(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (!ls->frame || idx < 0 || idx >= ls->frame->sprite_count)
        return luaL_error(L, "sprite index %d out of range", idx);

    const ExtractedSprite *s = &ls->frame->sprites[idx];
    lua_createtable(L, 0, 7);
    lua_pushinteger(L, s->screen_x);   lua_setfield(L, -2, "screen_x");
    lua_pushinteger(L, s->screen_y);   lua_setfield(L, -2, "screen_y");
    lua_pushinteger(L, s->width);      lua_setfield(L, -2, "width");
    lua_pushinteger(L, s->height);     lua_setfield(L, -2, "height");
    lua_pushinteger(L, s->tile_num);   lua_setfield(L, -2, "tile_num");
    lua_pushinteger(L, s->priority);   lua_setfield(L, -2, "priority");
    return 1;
}

static int l_sprite_setOffset(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= MAX_SPRITES) return luaL_error(L, "sprite index out of range");
    ls->sprite_overrides[idx].offset_x = (float)luaL_checknumber(L, 2);
    ls->sprite_overrides[idx].offset_y = (float)luaL_checknumber(L, 3);
    ls->sprite_overrides[idx].offset_z = (float)luaL_checknumber(L, 4);
    ls->sprite_overrides[idx].dirty = true;
    return 0;
}

static int l_sprite_setAlpha(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= MAX_SPRITES) return luaL_error(L, "sprite index out of range");
    ls->sprite_overrides[idx].alpha_mul = (float)luaL_checknumber(L, 2);
    ls->sprite_overrides[idx].dirty = true;
    return 0;
}

static int l_sprite_setHidden(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= MAX_SPRITES) return luaL_error(L, "sprite index out of range");
    ls->sprite_overrides[idx].hidden = lua_toboolean(L, 2);
    ls->sprite_overrides[idx].dirty = true;
    return 0;
}

static const luaL_Reg sprite_lib[] = {
    {"count", l_sprite_count},
    {"getInfo", l_sprite_getInfo},
    {"setOffset", l_sprite_setOffset},
    {"setAlpha", l_sprite_setAlpha},
    {"setHidden", l_sprite_setHidden},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/*  Lua API: frame.*                                                   */
/* ------------------------------------------------------------------ */

static int l_frame_number(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    lua_pushinteger(L, ls->frame_count);
    return 1;
}

static int l_frame_ppuMode(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    lua_pushinteger(L, ls->frame ? ls->frame->mode : 0);
    return 1;
}

static int l_frame_bgScroll(lua_State *L) {
    LuaScripting *ls = get_ls(L);
    int layer = (int)luaL_checkinteger(L, 1);
    if (layer < 0 || layer > 3) return luaL_error(L, "layer must be 0-3");
    if (!ls->frame) { lua_pushinteger(L, 0); lua_pushinteger(L, 0); return 2; }
    lua_pushinteger(L, ls->frame->bg_hscroll[layer]);
    lua_pushinteger(L, ls->frame->bg_vscroll[layer]);
    return 2;
}

static const luaL_Reg frame_lib[] = {
    {"number", l_frame_number},
    {"ppuMode", l_frame_ppuMode},
    {"bgScroll", l_frame_bgScroll},
    {NULL, NULL}
};

/* ------------------------------------------------------------------ */
/*  Lua API: log()                                                     */
/* ------------------------------------------------------------------ */

static int l_log(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    printf("[lua] %s\n", msg);
    fflush(stdout);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

static void register_api(LuaScripting *ls) {
    lua_State *L = ls->L;

    /* Store ls pointer in registry */
    lua_pushlightuserdata(L, ls);
    lua_setfield(L, LUA_REGISTRYINDEX, "_3dsnes_ls");

    /* snes.* */
    luaL_newlib(L, snes_lib);
    lua_setglobal(L, "snes");

    /* profile.* */
    luaL_newlib(L, profile_lib);
    lua_setglobal(L, "profile");

    /* camera.* */
    luaL_newlib(L, camera_lib);
    lua_setglobal(L, "camera");

    /* tile.* */
    luaL_newlib(L, tile_lib);
    lua_setglobal(L, "tile");

    /* sprite.* */
    luaL_newlib(L, sprite_lib);
    lua_setglobal(L, "sprite");

    /* frame.* */
    luaL_newlib(L, frame_lib);
    lua_setglobal(L, "frame");

    /* log() */
    lua_pushcfunction(L, l_log);
    lua_setglobal(L, "log");
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

bool lua_scripting_init(LuaScripting *ls) {
    memset(ls, 0, sizeof(*ls));
    return true;
}

void lua_scripting_shutdown(LuaScripting *ls) {
    lua_scripting_unload(ls);
}

bool lua_scripting_load(LuaScripting *ls, const char *script_path) {
    /* Check if file exists */
    struct stat st;
    if (stat(script_path, &st) != 0) return false;

    lua_scripting_unload(ls);

    ls->L = luaL_newstate();
    if (!ls->L) {
        set_error(ls, "Failed to create Lua state");
        return false;
    }

    luaL_openlibs(ls->L);
    register_api(ls);

    /* Set instruction limit to prevent infinite loops (1M instructions) */
    lua_sethook(ls->L, instruction_hook, LUA_MASKCOUNT, 1000000);

    /* Load and execute the script file */
    if (luaL_loadfile(ls->L, script_path) != LUA_OK) {
        const char *err = lua_tostring(ls->L, -1);
        set_error(ls, err ? err : "failed to load script");
        lua_close(ls->L);
        ls->L = NULL;
        return false;
    }

    /* Execute the script (defines global functions) */
    if (lua_pcall(ls->L, 0, 0, 0) != LUA_OK) {
        const char *err = lua_tostring(ls->L, -1);
        set_error(ls, err ? err : "failed to execute script");
        lua_close(ls->L);
        ls->L = NULL;
        return false;
    }

    /* Detect which hooks exist */
    lua_getglobal(ls->L, "Start");
    ls->has_start = lua_isfunction(ls->L, -1);
    lua_pop(ls->L, 1);

    lua_getglobal(ls->L, "Update");
    ls->has_update = lua_isfunction(ls->L, -1);
    lua_pop(ls->L, 1);

    lua_getglobal(ls->L, "LateUpdate");
    ls->has_late_update = lua_isfunction(ls->L, -1);
    lua_pop(ls->L, 1);

    lua_getglobal(ls->L, "End");
    ls->has_end = lua_isfunction(ls->L, -1);
    lua_pop(ls->L, 1);

    snprintf(ls->script_path, sizeof(ls->script_path), "%s", script_path);
    ls->script_mtime = (uint64_t)st.st_mtime;
    ls->loaded = true;
    ls->error_active = false;

    printf("[lua] Loaded script: %s\n", script_path);
    printf("[lua]   Start=%d Update=%d LateUpdate=%d End=%d\n",
           ls->has_start, ls->has_update, ls->has_late_update, ls->has_end);
    fflush(stdout);

    return true;
}

void lua_scripting_unload(LuaScripting *ls) {
    if (ls->L) {
        if (ls->loaded && ls->has_end) {
            lua_getglobal(ls->L, "End");
            safe_call(ls, 0, 0);
        }
        lua_close(ls->L);
        ls->L = NULL;
    }
    ls->loaded = false;
    ls->has_start = false;
    ls->has_update = false;
    ls->has_late_update = false;
    ls->has_end = false;
}

void lua_scripting_call_start(LuaScripting *ls) {
    if (!ls->loaded || !ls->has_start) return;
    lua_getglobal(ls->L, "Start");
    safe_call(ls, 0, 0);
}

void lua_scripting_call_update(LuaScripting *ls) {
    if (!ls->loaded || !ls->has_update) return;
    lua_getglobal(ls->L, "Update");
    safe_call(ls, 0, 0);
}

void lua_scripting_call_late_update(LuaScripting *ls) {
    if (!ls->loaded || !ls->has_late_update) return;
    lua_getglobal(ls->L, "LateUpdate");
    safe_call(ls, 0, 0);
}

void lua_scripting_call_end(LuaScripting *ls) {
    if (!ls->loaded || !ls->has_end) return;
    lua_getglobal(ls->L, "End");
    safe_call(ls, 0, 0);
}

void lua_scripting_clear_overrides(LuaScripting *ls) {
    for (int i = 0; i < MAX_BG_TILES; i++) {
        ls->tile_overrides[i].offset_x = 0;
        ls->tile_overrides[i].offset_y = 0;
        ls->tile_overrides[i].offset_z = 0;
        ls->tile_overrides[i].alpha_mul = 1.0f;
        ls->tile_overrides[i].hidden = false;
        ls->tile_overrides[i].dirty = false;
    }
    for (int i = 0; i < MAX_SPRITES; i++) {
        ls->sprite_overrides[i].offset_x = 0;
        ls->sprite_overrides[i].offset_y = 0;
        ls->sprite_overrides[i].offset_z = 0;
        ls->sprite_overrides[i].alpha_mul = 1.0f;
        ls->sprite_overrides[i].hidden = false;
        ls->sprite_overrides[i].dirty = false;
    }
}

bool lua_scripting_check_reload(LuaScripting *ls) {
    if (!ls->loaded || ls->script_path[0] == '\0') return false;
    uint64_t mtime = get_file_mtime(ls->script_path);
    if (mtime == 0 || mtime == ls->script_mtime) return false;

    printf("[lua] Script modified, reloading: %s\n", ls->script_path);
    fflush(stdout);

    char path_copy[512];
    snprintf(path_copy, sizeof(path_copy), "%s", ls->script_path);

    /* Save pointers that need to survive reload */
    Snes *snes = ls->snes;
    VoxelProfile *profile = ls->profile;
    Camera *cam = ls->camera;
    ExtractedFrame *frame = ls->frame;
    uint32_t fc = ls->frame_count;

    lua_scripting_unload(ls);
    lua_scripting_load(ls, path_copy);

    /* Restore pointers */
    ls->snes = snes;
    ls->profile = profile;
    ls->camera = cam;
    ls->frame = frame;
    ls->frame_count = fc;

    if (ls->loaded) {
        lua_scripting_call_start(ls);
        menu_show_toast("Script reloaded");
    }

    return true;
}
