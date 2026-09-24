#include "splats.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

namespace vox {
namespace {

constexpr int N = kChunkSize;
constexpr int P = N + 4;  // two voxels of padding for the normal estimate

enum Kind : uint8_t { kAir = 0, kOpaque = 1, kTransparent = 2 };

struct Context {
    const VoxelModel& model;
    std::vector<uint16_t> remap;
    std::vector<uint8_t> kind;
};

inline int pidx(int x, int y, int z) { return ((y + 2) * P + (z + 2)) * P + (x + 2); }

void fillPadded(const Context& ctx, IVec3 cc, uint16_t* pad) {
    for (int dy = -1; dy <= 1; ++dy)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const Chunk* c = ctx.model.chunk(cc.x + dx, cc.y + dy, cc.z + dz);
                auto range = [](int d, int& a, int& b) {
                    a = d < 0 ? -2 : d > 0 ? N : 0;
                    b = d < 0 ? -1 : d > 0 ? N + 1 : N - 1;
                };
                int x0, x1, y0, y1, z0, z1;
                range(dx, x0, x1);
                range(dy, y0, y1);
                range(dz, z0, z1);
                for (int y = y0; y <= y1; ++y)
                    for (int z = z0; z <= z1; ++z)
                        for (int x = x0; x <= x1; ++x)
                            pad[pidx(x, y, z)] =
                                c ? ctx.remap[c->v[Chunk::index(x & kChunkMask, y & kChunkMask, z & kChunkMask)]] : 0;
            }
}

uint32_t hash3(int x, int y, int z) {
    uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ uint32_t(z) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}

void buildChunk(const Context& ctx, IVec3 cc, uint16_t* pad, SplatChunk& out) {
    fillPadded(ctx, cc, pad);
    out.chunk = cc;
    static const int kFace[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int y = 0; y < N; ++y)
        for (int z = 0; z < N; ++z)
            for (int x = 0; x < N; ++x) {
                uint16_t c = pad[pidx(x, y, z)];
                if (!c) continue;
                // Visible if any face neighbor is air, or a different transparent block.
                bool visible = false;
                float ex = 0, ey = 0, ez = 0;
                for (const auto& f : kFace) {
                    uint16_t nb = pad[pidx(x + f[0], y + f[1], z + f[2])];
                    if (nb == 0 || (ctx.kind[nb] == kTransparent && nb != c)) {
                        visible = true;
                        ex += float(f[0]);
                        ey += float(f[1]);
                        ez += float(f[2]);
                    }
                }
                if (!visible) continue;
                // Smooth normal: points away from the solid mass in a 5x5x5 neighborhood.
                float nx = 0, ny = 0, nz = 0;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dz = -2; dz <= 2; ++dz)
                        for (int dx = -2; dx <= 2; ++dx) {
                            if (!pad[pidx(x + dx, y + dy, z + dz)]) continue;
                            float w = 1.0f / float(dx * dx + dy * dy + dz * dz + 1);
                            nx -= float(dx) * w;
                            ny -= float(dy) * w;
                            nz -= float(dz) * w;
                        }
                float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len < 0.3f) {  // thin or symmetric features: fall back to the exposed faces
                    nx = ex; ny = ey; nz = ez;
                    len = std::sqrt(nx * nx + ny * ny + nz * nz);
                }
                if (len > 0) { nx /= len; ny /= len; nz /= len; }

                const Material& m = ctx.model.materials[c];
                Splat s;
                s.x = uint8_t(x);
                s.y = uint8_t(y);
                s.z = uint8_t(z);
                s.seed = uint8_t(hash3(cc.x * N + x, cc.y * N + y, cc.z * N + z));
                s.nx = int8_t(std::lround(nx * 127.0f));
                s.ny = int8_t(std::lround(ny * 127.0f));
                s.nz = int8_t(std::lround(nz * 127.0f));
                s.flags = uint8_t(((m.flags & kMatEmissive) ? 1 : 0) | (ctx.kind[c] == kTransparent ? 2 : 0));
                s.r = m.color.r;
                s.g = m.color.g;
                s.b = m.color.b;
                s.a = ctx.kind[c] == kTransparent ? m.color.a : 255;
                out.splats.push_back(s);
            }
}

}  // namespace

std::vector<SplatChunk> buildSplats(const VoxelModel& model, const MeshOptions& opts) {
    Context ctx{model, {}, {}};
    size_t n = model.materials.size();
    ctx.remap.resize(n);
    ctx.kind.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Material& m = model.materials[i];
        bool hidden = i == 0 || (opts.hideDecorations && (m.flags & kMatDecoration));
        ctx.remap[i] = hidden ? 0 : uint16_t(i);
        ctx.kind[i] = hidden ? kAir : (m.flags & kMatTransparent) ? kTransparent : kOpaque;
    }
    std::vector<IVec3> coords;
    for (const auto& kv : model.chunks())
        if (kv.second->count) coords.push_back(chunkCoordFromKey(kv.first));

    std::vector<SplatChunk> chunks(coords.size());
    std::atomic<size_t> next{0};
    auto worker = [&] {
        std::vector<uint16_t> pad(size_t(P) * P * P);
        for (;;) {
            size_t k = next.fetch_add(1);
            if (k >= coords.size()) break;
            buildChunk(ctx, coords[k], pad.data(), chunks[k]);
        }
    };
    unsigned threads = std::max(1u, std::min(std::thread::hardware_concurrency(), unsigned(coords.size())));
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();

    std::vector<SplatChunk> result;
    for (auto& c : chunks)
        if (!c.splats.empty()) result.push_back(std::move(c));
    return result;
}

}  // namespace vox
