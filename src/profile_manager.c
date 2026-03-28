/*
 * profile_manager.c — Per-game voxel profile JSON load/save.
 */

#include "3dsnes/profile_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

/* ── ROM Header Reading (simplified from LakeSnes snes_other.c) ──── */

typedef struct {
    char name[22];
    uint16_t checksum;
    int score;
} MiniHeader;

static void read_mini_header(const uint8_t *data, int length, int location,
                              MiniHeader *h) {
    /* Read internal name (21 ASCII chars) */
    for (int i = 0; i < 21; i++) {
        uint8_t ch = data[location + i];
        h->name[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '.';
    }
    h->name[21] = '\0';

    /* Read checksum */
    h->checksum = (uint16_t)(data[location + 0x1e] | (data[location + 0x1f] << 8));

    /* Score the header (same logic as LakeSnes) */
    int score = 0;
    uint8_t speed = data[location + 0x15] >> 4;
    uint8_t type = data[location + 0x15] & 0xf;
    uint8_t coprocessor = data[location + 0x16] >> 4;
    uint8_t chips = data[location + 0x16] & 0xf;
    uint8_t region = data[location + 0x19];
    uint16_t complement = (uint16_t)(data[location + 0x1c] | (data[location + 0x1d] << 8));

    score += (speed == 2 || speed == 3) ? 5 : -4;
    score += (type <= 3 || type == 5) ? 5 : -2;
    score += (coprocessor <= 5 || coprocessor >= 0xe) ? 5 : -2;
    score += (chips <= 6 || chips == 9 || chips == 0xa) ? 5 : -2;
    score += (region <= 0x14) ? 5 : -2;
    score += (h->checksum + complement == 0xffff) ? 8 : -6;

    uint16_t resetVector = (uint16_t)(data[location + 0x3c] | (data[location + 0x3d] << 8));
    score += (resetVector >= 0x8000) ? 8 : -20;

    int opLoc = location + 0x40 - 0x8000 + (resetVector & 0x7fff);
    if (opLoc >= 0 && opLoc < length) {
        uint8_t op = data[opLoc];
        if (op == 0x78 || op == 0x18) score += 6;
        if (op == 0x4c || op == 0x5c || op == 0x9c) score += 3;
        if (op == 0x00 || op == 0xff || op == 0xdb) score -= 6;
    } else {
        score -= 14;
    }

    h->score = score;
}

void profile_read_rom_identity(const uint8_t *rom_data, int rom_size,
                                char *name_out, uint16_t *checksum_out) {
    MiniHeader headers[6];
    for (int i = 0; i < 6; i++) headers[i].score = -50;

    /* Try all 6 possible header locations */
    if (rom_size >= 0x8000)   read_mini_header(rom_data, rom_size, 0x7fc0, &headers[0]);
    if (rom_size >= 0x8200)   read_mini_header(rom_data, rom_size, 0x81c0, &headers[1]);
    if (rom_size >= 0x10000)  read_mini_header(rom_data, rom_size, 0xffc0, &headers[2]);
    if (rom_size >= 0x10200)  read_mini_header(rom_data, rom_size, 0x101c0, &headers[3]);
    if (rom_size >= 0x410000) read_mini_header(rom_data, rom_size, 0x40ffc0, &headers[4]);
    if (rom_size >= 0x410200) read_mini_header(rom_data, rom_size, 0x4101c0, &headers[5]);

    /* Pick best scoring header */
    int best = 0;
    for (int i = 5; i >= 0; i--) {
        if (headers[i].score > headers[best].score) best = i;
    }

    /* Trim trailing spaces from name */
    strncpy(name_out, headers[best].name, 21);
    name_out[21] = '\0';
    int len = (int)strlen(name_out);
    while (len > 0 && name_out[len - 1] == ' ') name_out[--len] = '\0';

    *checksum_out = headers[best].checksum;
}

/* ── Path Building ───────────────────────────────────────────────── */

void profile_build_path(char *out, int out_size,
                        const char *rom_name, uint16_t checksum) {
    char sanitized[64];
    int j = 0;
    for (int i = 0; rom_name[i] && j < 50; i++) {
        char c = rom_name[i];
        if ((c >= 'A' && c <= 'Z')) {
            sanitized[j++] = c + 32; /* lowercase */
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            sanitized[j++] = c;
        } else {
            if (j > 0 && sanitized[j - 1] != '_') sanitized[j++] = '_';
        }
    }
    /* Trim trailing underscore */
    while (j > 0 && sanitized[j - 1] == '_') j--;
    sanitized[j] = '\0';

    snprintf(out, out_size, "profiles/%s_%04x.json", sanitized, checksum);
}

/* ── JSON Parsing (hand-written, format is flat + simple) ────────── */

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static const char *find_key(const char *json, const char *key) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    p = skip_ws(p);
    if (*p == ':') p++;
    return skip_ws(p);
}

static float parse_float(const char *p) {
    return (float)strtod(p, NULL);
}

static int parse_int(const char *p) {
    return (int)strtol(p, NULL, 0);
}

static bool parse_float_array(const char *p, float *out, int count) {
    if (*p != '[') return false;
    p++;
    for (int i = 0; i < count; i++) {
        p = skip_ws(p);
        out[i] = (float)strtod(p, NULL);
        /* Skip past the number */
        while (*p && *p != ',' && *p != ']') p++;
        if (*p == ',') p++;
    }
    return true;
}

static bool parse_int_array(const char *p, int *out, int count) {
    if (*p != '[') return false;
    p++;
    for (int i = 0; i < count; i++) {
        p = skip_ws(p);
        out[i] = (int)strtol(p, NULL, 0);
        while (*p && *p != ',' && *p != ']') p++;
        if (*p == ',') p++;
    }
    return true;
}

bool profile_load_json(const char *path, VoxelProfile *out) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 8192) { fclose(f); return false; }

    char *json = (char *)malloc(size + 1);
    fread(json, 1, size, f);
    json[size] = '\0';
    fclose(f);

    /* Start with generic defaults */
    *out = voxel_profile_generic();

    const char *v;

    if ((v = find_key(json, "bg_z")))           parse_float_array(v, out->bg_z, 4);
    if ((v = find_key(json, "bg_depth")))        parse_float_array(v, out->bg_depth, 4);
    if ((v = find_key(json, "sprite_z")))        out->sprite_z = parse_float(v);
    if ((v = find_key(json, "sprite_depth")))    out->sprite_depth = parse_float(v);
    if ((v = find_key(json, "pixel_scale")))     out->pixel_scale = parse_float(v);
    if ((v = find_key(json, "brightness_depth")))out->brightness_depth = parse_float(v);
    if ((v = find_key(json, "bg_skip_layer")))   out->bg_skip_layer = parse_int(v);
    if ((v = find_key(json, "light_dir")))      parse_float_array(v, out->light_dir, 3);
    if ((v = find_key(json, "ambient")))         out->ambient = parse_float(v);
    if ((v = find_key(json, "diffuse")))         out->diffuse = parse_float(v);
    if ((v = find_key(json, "layer_alpha")))     parse_float_array(v, out->layer_alpha, 4);
    if ((v = find_key(json, "sprite_alpha")))    out->sprite_alpha = parse_float(v);
    if ((v = find_key(json, "sky_type")))        out->sky_type = parse_int(v);
    {
        int tmp[3];
        if ((v = find_key(json, "sky_top")) && parse_int_array(v, tmp, 3)) {
            out->sky_top[0] = (uint8_t)tmp[0];
            out->sky_top[1] = (uint8_t)tmp[1];
            out->sky_top[2] = (uint8_t)tmp[2];
        }
        if ((v = find_key(json, "sky_bot")) && parse_int_array(v, tmp, 3)) {
            out->sky_bot[0] = (uint8_t)tmp[0];
            out->sky_bot[1] = (uint8_t)tmp[1];
            out->sky_bot[2] = (uint8_t)tmp[2];
        }
    }
    if ((v = find_key(json, "shadows_enabled")))  out->shadows_enabled = parse_int(v) != 0;
    if ((v = find_key(json, "shadow_opacity")))   out->shadow_opacity = parse_float(v);
    if ((v = find_key(json, "shadow_y")))         out->shadow_y = parse_float(v);
    if ((v = find_key(json, "sprite_grouping")))  out->sprite_grouping = parse_int(v) != 0;
    if ((v = find_key(json, "group_gap")))        out->group_gap = parse_int(v);

    free(json);
    return true;
}

/* ── JSON Writing ────────────────────────────────────────────────── */

static void ensure_profiles_dir(void) {
    struct stat st;
    if (stat("profiles", &st) != 0) {
#ifdef _WIN32
        _mkdir("profiles");
#else
        mkdir("profiles", 0755);
#endif
    }
}

bool profile_save_json(const char *path, const VoxelProfile *profile,
                       const char *rom_name, uint16_t checksum) {
    ensure_profiles_dir();
    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "{\n");
    fprintf(f, "    \"rom_name\": \"%s\",\n", rom_name);
    fprintf(f, "    \"rom_checksum\": \"0x%04X\",\n", checksum);
    fprintf(f, "    \"bg_z\": [%.1f, %.1f, %.1f, %.1f],\n",
            profile->bg_z[0], profile->bg_z[1], profile->bg_z[2], profile->bg_z[3]);
    fprintf(f, "    \"bg_depth\": [%.1f, %.1f, %.1f, %.1f],\n",
            profile->bg_depth[0], profile->bg_depth[1], profile->bg_depth[2], profile->bg_depth[3]);
    fprintf(f, "    \"sprite_z\": %.1f,\n", profile->sprite_z);
    fprintf(f, "    \"sprite_depth\": %.1f,\n", profile->sprite_depth);
    fprintf(f, "    \"pixel_scale\": %.2f,\n", profile->pixel_scale);
    fprintf(f, "    \"brightness_depth\": %.2f,\n", profile->brightness_depth);
    fprintf(f, "    \"bg_skip_layer\": %d,\n", profile->bg_skip_layer);
    fprintf(f, "    \"light_dir\": [%.4f, %.4f, %.4f],\n",
            profile->light_dir[0], profile->light_dir[1], profile->light_dir[2]);
    fprintf(f, "    \"ambient\": %.4f,\n", profile->ambient);
    fprintf(f, "    \"diffuse\": %.4f,\n", profile->diffuse);
    fprintf(f, "    \"layer_alpha\": [%.4f, %.4f, %.4f, %.4f],\n",
            profile->layer_alpha[0], profile->layer_alpha[1],
            profile->layer_alpha[2], profile->layer_alpha[3]);
    fprintf(f, "    \"sprite_alpha\": %.4f,\n", profile->sprite_alpha);
    fprintf(f, "    \"sky_type\": %d,\n", profile->sky_type);
    fprintf(f, "    \"sky_top\": [%d, %d, %d],\n",
            profile->sky_top[0], profile->sky_top[1], profile->sky_top[2]);
    fprintf(f, "    \"sky_bot\": [%d, %d, %d],\n",
            profile->sky_bot[0], profile->sky_bot[1], profile->sky_bot[2]);
    fprintf(f, "    \"shadows_enabled\": %d,\n", profile->shadows_enabled ? 1 : 0);
    fprintf(f, "    \"shadow_opacity\": %.4f,\n", profile->shadow_opacity);
    fprintf(f, "    \"shadow_y\": %.4f,\n", profile->shadow_y);
    fprintf(f, "    \"sprite_grouping\": %d,\n", profile->sprite_grouping ? 1 : 0);
    fprintf(f, "    \"group_gap\": %d\n", profile->group_gap);
    fprintf(f, "}\n");

    fclose(f);
    return true;
}
