#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "nbt.h"
#include "voxel_model.h"

namespace vox {

// Loads any supported file, detecting the format from its content. Throws std::runtime_error.
std::unique_ptr<VoxelModel> loadModelFile(const std::string& path);

// MagicaVoxel .vox
std::unique_ptr<VoxelModel> loadVox(const std::vector<uint8_t>& data);

// Minecraft NBT based formats: WorldEdit/MCEdit .schematic, Sponge .schem (v1-v3),
// Litematica .litematic and vanilla structure block .nbt.
std::unique_ptr<VoxelModel> loadMinecraftNbt(const nbt::Tag& root);

bool hasSupportedExtension(const std::string& path);

}  // namespace vox
