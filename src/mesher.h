// Converts voxel chunks into compact triangle meshes (greedy meshing + ambient occlusion).
#pragma once

#include <cstdint>
#include <vector>

#include "voxel_model.h"

namespace vox {

// 8 bytes per vertex. Positions are local to the chunk (0..32).
struct PackedVertex {
    uint8_t x, y, z;
    uint8_t normalAo;  // bits 0-2: face direction (0..5), bits 3-4: ambient occlusion (0..3), bit 5: emissive
    uint8_t r, g, b, a;
};

// Each quad is 4 vertices; draw with the shared quad index pattern (0,1,2, 0,2,3).
struct ChunkMesh {
    IVec3 chunk;  // chunk coordinates
    std::vector<PackedVertex> opaque;
    std::vector<PackedVertex> transparent;
};

struct MeshOptions {
    bool hideDecorations = false;
    bool greedy = true;
    bool smooth = false;         // use the smooth (surface nets) mesher instead of cubes
    int smoothIterations = 8;    // relaxation passes for the smooth mesher
};

// Builds a mesh for every non-empty chunk, using all hardware threads.
std::vector<ChunkMesh> buildMeshes(const VoxelModel& model, const MeshOptions& opts);

}  // namespace vox
