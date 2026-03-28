/*
 * profile_manager.h — Per-game voxel profile load/save.
 *
 * Profiles are stored as JSON files in the profiles/ directory,
 * keyed by sanitized ROM internal name + checksum.
 */

#ifndef PROFILE_MANAGER_H
#define PROFILE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "3dsnes/voxelizer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Extract ROM internal name (21 chars, trimmed) and checksum from raw ROM data.
 * Reads the SNES header directly. */
void profile_read_rom_identity(const uint8_t *rom_data, int rom_size,
                                char *name_out, uint16_t *checksum_out);

/* Build the profile file path from ROM identity.
 * Result: "profiles/<sanitized_name>_<checksum_hex>.json" */
void profile_build_path(char *out, int out_size,
                        const char *rom_name, uint16_t checksum);

/* Load a VoxelProfile from a JSON file. Returns true on success. */
bool profile_load_json(const char *path, VoxelProfile *out);

/* Save a VoxelProfile to a JSON file. Returns true on success. */
bool profile_save_json(const char *path, const VoxelProfile *profile,
                       const char *rom_name, uint16_t checksum);

#ifdef __cplusplus
}
#endif

#endif /* PROFILE_MANAGER_H */
