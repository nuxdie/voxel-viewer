// Approximate colors for Minecraft blocks (average texture color).
#pragma once

#include <string>

#include "voxel_model.h"

namespace vox {

struct BlockInfo {
    Color color;
    uint8_t flags = 0;       // MaterialFlags
    bool invisible = false;  // air, barrier, structure_void, light...
};

// Accepts "minecraft:oak_log[axis=y]", "oak_log", etc.
BlockInfo lookupBlock(const std::string& blockState);

// Strips the namespace and block-state properties: "minecraft:oak_log[axis=y]" -> "oak_log".
std::string baseBlockName(const std::string& blockState);

// Maps a pre-1.13 numeric block id + data value to a modern block name.
std::string legacyBlockName(int id, int data);

}  // namespace vox
