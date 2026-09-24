// Smooth meshing: constrained elastic surface nets.
//
// A surface-nets mesh is built over voxel centers, then each vertex is repeatedly moved toward
// the average of its neighbors while being clamped to its own cell. Stair steps and corners get
// rounded, but features one voxel thick (walls, fences, pillars) are preserved.
#pragma once

#include <cstdint>
#include <vector>

#include "mesher.h"
#include "voxel_model.h"

namespace vox {

// 20 bytes per vertex. Position is chunk-local fixed point: local = x / 1024 - 2.
struct SmoothVertex {
    uint16_t x, y, z;
    int8_t nx, ny, nz;
    uint8_t aoEmissive;  // bits 0-6: ambient light 0..127, bit 7: emissive
    uint8_t r, g, b, a;
    uint8_t lr, lg, lb;  // block light, 0-255 (light level * 17)
    uint8_t surface;     // see packSurface()
    uint16_t pad;
};

struct SmoothMesh {
    std::vector<SmoothVertex> vertices;
    std::vector<uint32_t> indices;  // triangles
};

struct SmoothChunkMesh {
    IVec3 chunk;
    SmoothMesh opaque, transparent;
};

constexpr int kMaxSmoothIterations = 16;

// Builds smooth meshes for every non-empty chunk, using all hardware threads.
// iterations: relaxation passes (0 = plain surface nets / chamfered look, 8 = default).
std::vector<SmoothChunkMesh> buildSmoothMeshes(const VoxelModel& model, const MeshOptions& opts, int iterations);

}  // namespace vox
