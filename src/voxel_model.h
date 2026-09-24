// Core voxel data model: sparse chunked grid of palette indices.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vox {

constexpr int kChunkBits = 5;
constexpr int kChunkSize = 1 << kChunkBits;  // 32
constexpr int kChunkMask = kChunkSize - 1;
constexpr int kChunkVolume = kChunkSize * kChunkSize * kChunkSize;

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

enum MaterialFlags : uint8_t {
    kMatTransparent = 1 << 0,  // alpha-blended (glass, water, ice)
    kMatEmissive = 1 << 1,     // unlit, full brightness
    kMatDecoration = 1 << 2,   // small non-cube block (torch, flower, rail...)
};

struct Material {
    Color color;
    uint8_t flags = 0;
    std::string name;  // e.g. "minecraft:stone" or "#12" for .vox palette entries
};

struct IVec3 {
    int x = 0, y = 0, z = 0;
};

struct Chunk {
    uint16_t v[kChunkVolume] = {};
    uint32_t count = 0;  // number of non-air voxels
    static int index(int lx, int ly, int lz) { return (ly * kChunkSize + lz) * kChunkSize + lx; }
};

inline uint64_t chunkKey(int cx, int cy, int cz) {
    constexpr int64_t kBias = 1 << 20;
    return (uint64_t(cx + kBias) << 42) | (uint64_t(cy + kBias) << 21) | uint64_t(cz + kBias);
}
inline IVec3 chunkCoordFromKey(uint64_t k) {
    constexpr int64_t kBias = 1 << 20;
    constexpr uint64_t m = (1u << 21) - 1;
    return {int(int64_t((k >> 42) & m) - kBias), int(int64_t((k >> 21) & m) - kBias), int(int64_t(k & m) - kBias)};
}

// Y is up. Index 0 of the palette is always air.
class VoxelModel {
public:
    VoxelModel() { materials.push_back(Material{{0, 0, 0, 0}, 0, "air"}); }

    // Coordinates beyond +-kMaxCoord are ignored (they only occur in corrupt files).
    static constexpr int kMaxCoord = 1 << 24;

    void set(int x, int y, int z, uint16_t value) {
        if (x <= -kMaxCoord || x >= kMaxCoord || y <= -kMaxCoord || y >= kMaxCoord || z <= -kMaxCoord || z >= kMaxCoord)
            return;
        uint64_t key = chunkKey(x >> kChunkBits, y >> kChunkBits, z >> kChunkBits);
        Chunk* c = key == lastKey_ ? lastChunk_ : nullptr;
        if (!c) {
            auto it = chunks_.find(key);
            if (it == chunks_.end()) {
                if (value == 0) return;
                c = (chunks_[key] = std::make_unique<Chunk>()).get();
            } else {
                c = it->second.get();
            }
            lastKey_ = key;
            lastChunk_ = c;
        }
        uint16_t& slot = c->v[Chunk::index(x & kChunkMask, y & kChunkMask, z & kChunkMask)];
        if (slot == 0 && value != 0) { ++c->count; ++voxelCount_; }
        if (slot != 0 && value == 0) { --c->count; --voxelCount_; }
        slot = value;
        if (value != 0) growBounds(x, y, z);
    }

    uint16_t get(int x, int y, int z) const {
        const Chunk* c = chunk(x >> kChunkBits, y >> kChunkBits, z >> kChunkBits);
        return c ? c->v[Chunk::index(x & kChunkMask, y & kChunkMask, z & kChunkMask)] : 0;
    }

    const Chunk* chunk(int cx, int cy, int cz) const {
        auto it = chunks_.find(chunkKey(cx, cy, cz));
        return it == chunks_.end() ? nullptr : it->second.get();
    }

    const std::unordered_map<uint64_t, std::unique_ptr<Chunk>>& chunks() const { return chunks_; }

    size_t voxelCount() const { return voxelCount_; }
    bool empty() const { return voxelCount_ == 0; }
    // Inclusive bounds of occupied voxels (valid only if !empty()).
    IVec3 boundsMin() const { return min_; }
    IVec3 boundsMax() const { return max_; }
    IVec3 size() const {
        if (empty()) return {0, 0, 0};
        return {max_.x - min_.x + 1, max_.y - min_.y + 1, max_.z - min_.z + 1};
    }

    std::vector<Material> materials;
    std::string format;  // human readable source format, e.g. "MagicaVoxel .vox (v150)"

private:
    void growBounds(int x, int y, int z) {
        if (!hasBounds_) {
            min_ = max_ = {x, y, z};
            hasBounds_ = true;
            return;
        }
        if (x < min_.x) min_.x = x;
        if (y < min_.y) min_.y = y;
        if (z < min_.z) min_.z = z;
        if (x > max_.x) max_.x = x;
        if (y > max_.y) max_.y = y;
        if (z > max_.z) max_.z = z;
    }

    std::unordered_map<uint64_t, std::unique_ptr<Chunk>> chunks_;
    uint64_t lastKey_ = ~uint64_t(0);  // cache for sequential writes
    Chunk* lastChunk_ = nullptr;
    size_t voxelCount_ = 0;
    bool hasBounds_ = false;
    IVec3 min_, max_;
};

}  // namespace vox
