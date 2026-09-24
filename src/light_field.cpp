#include "light_field.h"

#include <algorithm>
#include <cmath>

namespace vox {
namespace {

struct Node {
    int x, y, z;
};

}  // namespace

std::shared_ptr<LightField> LightField::build(const VoxelModel& model) {
    bool any = false;
    for (const Material& m : model.materials) any |= m.emission > 0;
    if (!any) return nullptr;

    auto field = std::make_shared<LightField>();
    auto& chunks = field->chunks_;

    // Lookups with a one-entry cache: the flood fill mostly walks within a chunk.
    uint64_t lastKey = ~uint64_t(0);
    ChunkData* lastData = nullptr;
    auto slot = [&](int x, int y, int z) -> uint16_t& {
        uint64_t key = chunkKey(x >> kChunkBits, y >> kChunkBits, z >> kChunkBits);
        if (key != lastKey) {
            auto& p = chunks[key];
            if (!p) {
                p = std::make_unique<ChunkData>();
                p->fill(0);
            }
            lastKey = key;
            lastData = p.get();
        }
        return (*lastData)[size_t(Chunk::index(x & kChunkMask, y & kChunkMask, z & kChunkMask))];
    };
    std::vector<uint8_t> blocks(model.materials.size());
    for (size_t i = 1; i < model.materials.size(); ++i) blocks[i] = blocksLight(model.materials[i]);

    // Seed every emitter with its level, tinted by its color (brightest channel = full level).
    std::vector<Node> queue;
    for (const auto& kv : model.chunks()) {
        IVec3 cc = chunkCoordFromKey(kv.first);
        const Chunk& ch = *kv.second;
        for (int i = 0; i < kChunkVolume; ++i) {
            const Material& m = model.materials[ch.v[i]];
            if (!ch.v[i] || !m.emission) continue;
            // Tint: the material's hue, pulled a little toward warm white so dim colors still light.
            float r = float(m.color.r), g = float(m.color.g), b = float(m.color.b);
            float mx = std::max({r, g, b, 1.0f});
            r = 0.65f * r / mx + 0.35f * 1.0f;
            g = 0.65f * g / mx + 0.35f * 0.85f;
            b = 0.65f * b / mx + 0.35f * 0.65f;
            mx = std::max({r, g, b});
            int lr = int(std::lround(m.emission * r / mx)), lg = int(std::lround(m.emission * g / mx)),
                lb = int(std::lround(m.emission * b / mx));
            int x = cc.x * kChunkSize + (i & kChunkMask);
            int z = cc.z * kChunkSize + ((i >> kChunkBits) & kChunkMask);
            int y = cc.y * kChunkSize + (i >> (2 * kChunkBits));
            slot(x, y, z) = packLight(lr, lg, lb);
            queue.push_back({x, y, z});
            ++field->emitters_;
        }
    }

    // Breadth-first flood fill; a cell is revisited only when some channel gets brighter.
    static const int kDir[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    size_t head = 0;
    while (head < queue.size()) {
        Node n = queue[head++];
        uint16_t v = slot(n.x, n.y, n.z);
        int r = lightR(v) - 1, g = lightG(v) - 1, b = lightB(v) - 1;
        if (r <= 0 && g <= 0 && b <= 0) continue;
        for (const auto& d : kDir) {
            int x = n.x + d[0], y = n.y + d[1], z = n.z + d[2];
            if (std::abs(x) >= VoxelModel::kMaxCoord || std::abs(y) >= VoxelModel::kMaxCoord ||
                std::abs(z) >= VoxelModel::kMaxCoord)
                continue;
            if (blocks[model.get(x, y, z)]) continue;
            uint16_t& cur = slot(x, y, z);
            int cr = lightR(cur), cg = lightG(cur), cb = lightB(cur);
            if (r > cr || g > cg || b > cb) {
                cur = packLight(std::max(r, cr), std::max(g, cg), std::max(b, cb));
                queue.push_back({x, y, z});
            }
        }
        // Keep the queue from growing without bound on big scenes.
        if (head > (1u << 20) && head * 2 > queue.size()) {
            queue.erase(queue.begin(), queue.begin() + long(head));
            head = 0;
        }
    }
    return field;
}

void LightField::fillPadded(IVec3 cc, int pad, uint16_t* out) const {
    const int S = kChunkSize + 2 * pad;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const ChunkData* c = chunk(cc.x + dx, cc.y + dy, cc.z + dz);
                auto range = [&](int d, int& a, int& b) {
                    a = d < 0 ? -pad : d > 0 ? kChunkSize : 0;
                    b = d < 0 ? -1 : d > 0 ? kChunkSize + pad - 1 : kChunkSize - 1;
                };
                int x0, x1, y0, y1, z0, z1;
                range(dx, x0, x1);
                range(dy, y0, y1);
                range(dz, z0, z1);
                for (int y = y0; y <= y1; ++y)
                    for (int z = z0; z <= z1; ++z)
                        for (int x = x0; x <= x1; ++x)
                            out[((y + pad) * S + (z + pad)) * S + (x + pad)] =
                                c ? (*c)[size_t(Chunk::index(x & kChunkMask, y & kChunkMask, z & kChunkMask))] : 0;
            }
}

}  // namespace vox
