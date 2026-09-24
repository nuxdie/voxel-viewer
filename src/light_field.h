// Block light: colored light flooded from emissive voxels, Minecraft style.
//
// Every emitter starts at its light level (0-15) tinted by its color, and light loses one level
// per step as it spreads through air, glass, water and small decorations. Solid blocks stop it.
// Levels are stored per voxel as 4:4:4 RGB in a uint16_t, in sparse 32^3 chunks.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "voxel_model.h"

namespace vox {

// True if a material stops light (opaque full blocks; glass, water and decorations let it through).
inline bool blocksLight(const Material& m) { return (m.flags & (kMatTransparent | kMatDecoration)) == 0; }

inline int lightR(uint16_t v) { return v & 15; }
inline int lightG(uint16_t v) { return (v >> 4) & 15; }
inline int lightB(uint16_t v) { return (v >> 8) & 15; }
inline uint16_t packLight(int r, int g, int b) { return uint16_t(r | g << 4 | b << 8); }

class LightField {
public:
    using ChunkData = std::array<uint16_t, kChunkVolume>;

    // Returns nullptr when the model has no light-emitting voxels.
    static std::shared_ptr<LightField> build(const VoxelModel& model);

    uint16_t get(int x, int y, int z) const {
        const ChunkData* c = chunk(x >> kChunkBits, y >> kChunkBits, z >> kChunkBits);
        return c ? (*c)[size_t(Chunk::index(x & kChunkMask, y & kChunkMask, z & kChunkMask))] : 0;
    }
    const ChunkData* chunk(int cx, int cy, int cz) const {
        auto it = chunks_.find(chunkKey(cx, cy, cz));
        return it == chunks_.end() ? nullptr : it->second.get();
    }
    size_t emitterCount() const { return emitters_; }
    size_t litChunkCount() const { return chunks_.size(); }

    // Copies light around chunk `cc` into out, indexed ((y+pad)*S + (z+pad))*S + (x+pad), S = 32+2*pad.
    void fillPadded(IVec3 cc, int pad, uint16_t* out) const;

private:
    std::unordered_map<uint64_t, std::unique_ptr<ChunkData>> chunks_;
    size_t emitters_ = 0;
};

}  // namespace vox
