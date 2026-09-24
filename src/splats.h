// Splats for the watercolor renderer: one soft, camera-facing blob per visible surface voxel.
#pragma once

#include <cstdint>
#include <vector>

#include "mesher.h"
#include "voxel_model.h"

namespace vox {

// 12 bytes per splat. Position is the voxel's chunk-local coordinate (0..31).
struct Splat {
    uint8_t x, y, z;
    uint8_t seed;         // random per voxel, drives the blob's shape and pigment variation
    int8_t nx, ny, nz;    // smoothed surface normal (snorm8)
    uint8_t flags;        // bit 0: emissive, bit 1: transparent
    uint8_t r, g, b, a;
};

struct SplatChunk {
    IVec3 chunk;
    std::vector<Splat> splats;
};

// Builds splats for every non-empty chunk, using all hardware threads.
std::vector<SplatChunk> buildSplats(const VoxelModel& model, const MeshOptions& opts);

}  // namespace vox
