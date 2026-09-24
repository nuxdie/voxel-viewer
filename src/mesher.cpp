#include "mesher.h"

#include <atomic>
#include <cstdlib>
#include <thread>

namespace vox {
namespace {

constexpr int N = kChunkSize;
constexpr int P = N + 2;  // padded size

enum Kind : uint8_t { kAir = 0, kOpaque = 1, kTransparent = 2 };

// Face directions: +X, -X, +Y, -Y, +Z, -Z. For each: normal axis, sign, and tangent axes (u, v)
// chosen so that u x v = normal, which makes the (0,0),(1,0),(1,1),(0,1) corner order CCW.
struct Dir {
    int axis, sign, u, v;
};
constexpr Dir kDirs[6] = {
    {0, +1, 1, 2}, {0, -1, 2, 1}, {1, +1, 2, 0}, {1, -1, 0, 2}, {2, +1, 0, 1}, {2, -1, 1, 0},
};
constexpr int kCornerU[4] = {0, 1, 1, 0};
constexpr int kCornerV[4] = {0, 0, 1, 1};

struct Context {
    const VoxelModel& model;
    std::vector<uint16_t> remap;  // palette index -> palette index (0 hides it)
    std::vector<uint8_t> kind;
};

inline int pidx(int x, int y, int z) { return ((y + 1) * P + (z + 1)) * P + (x + 1); }

void fillPadded(const Context& ctx, IVec3 cc, uint16_t* pad) {
    for (int dy = -1; dy <= 1; ++dy)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const Chunk* c = ctx.model.chunk(cc.x + dx, cc.y + dy, cc.z + dz);
                // Range of local coords (in the padded chunk frame) covered by this neighbor.
                int x0 = dx < 0 ? -1 : dx > 0 ? N : 0, x1 = dx < 0 ? -1 : dx > 0 ? N : N - 1;
                int y0 = dy < 0 ? -1 : dy > 0 ? N : 0, y1 = dy < 0 ? -1 : dy > 0 ? N : N - 1;
                int z0 = dz < 0 ? -1 : dz > 0 ? N : 0, z1 = dz < 0 ? -1 : dz > 0 ? N : N - 1;
                for (int y = y0; y <= y1; ++y)
                    for (int z = z0; z <= z1; ++z)
                        for (int x = x0; x <= x1; ++x) {
                            uint16_t v = 0;
                            if (c) v = ctx.remap[c->v[Chunk::index(x & kChunkMask, y & kChunkMask, z & kChunkMask)]];
                            pad[pidx(x, y, z)] = v;
                        }
            }
}

void emitQuad(std::vector<PackedVertex>& out, const Dir& d, int plane, int i, int j, int w, int h,
              const uint8_t ao[4], const Material& m, bool emissive, uint8_t dirIndex) {
    // Choose the triangulation diagonal that isolates the odd corner to avoid AO anisotropy.
    bool flip = std::abs(ao[0] - ao[2]) > std::abs(ao[1] - ao[3]);
    for (int k = 0; k < 4; ++k) {
        int c = flip ? (k + 1) & 3 : k;
        int pos[3];
        pos[d.axis] = plane;
        pos[d.u] = i + kCornerU[c] * w;
        pos[d.v] = j + kCornerV[c] * h;
        PackedVertex v;
        v.x = uint8_t(pos[0]);
        v.y = uint8_t(pos[1]);
        v.z = uint8_t(pos[2]);
        v.normalAo = uint8_t(dirIndex | (ao[c] << 3) | (emissive ? 32 : 0));
        v.r = m.color.r;
        v.g = m.color.g;
        v.b = m.color.b;
        v.a = (m.flags & kMatTransparent) ? m.color.a : 255;
        out.push_back(v);
    }
}

void meshChunk(const Context& ctx, IVec3 cc, uint16_t* pad, uint32_t* mask, bool greedy, ChunkMesh& out) {
    fillPadded(ctx, cc, pad);
    out.chunk = cc;
    const auto& kind = ctx.kind;

    for (uint8_t di = 0; di < 6; ++di) {
        const Dir& d = kDirs[di];
        int nrm[3] = {0, 0, 0};
        nrm[d.axis] = d.sign;
        int du[3] = {0, 0, 0}, dv[3] = {0, 0, 0};
        du[d.u] = 1;
        dv[d.v] = 1;

        for (int s = 0; s < N; ++s) {
            bool any = false;
            for (int j = 0; j < N; ++j)
                for (int i = 0; i < N; ++i) {
                    int p[3];
                    p[d.axis] = s;
                    p[d.u] = i;
                    p[d.v] = j;
                    uint16_t c = pad[pidx(p[0], p[1], p[2])];
                    uint32_t key = 0;
                    if (c) {
                        int q[3] = {p[0] + nrm[0], p[1] + nrm[1], p[2] + nrm[2]};
                        uint16_t nb = pad[pidx(q[0], q[1], q[2])];
                        bool visible = nb == 0 || (kind[nb] == kTransparent && nb != c);
                        if (visible) {
                            uint32_t aoBits = 0xFF;  // all corners fully lit
                            if (kind[c] == kOpaque) {
                                auto occ = [&](int ou, int ov) {
                                    int r[3] = {q[0] + du[0] * ou + dv[0] * ov, q[1] + du[1] * ou + dv[1] * ov,
                                                q[2] + du[2] * ou + dv[2] * ov};
                                    return kind[pad[pidx(r[0], r[1], r[2])]] == kOpaque ? 1 : 0;
                                };
                                aoBits = 0;
                                for (int k = 0; k < 4; ++k) {
                                    int su = kCornerU[k] ? 1 : -1, sv = kCornerV[k] ? 1 : -1;
                                    int s1 = occ(su, 0), s2 = occ(0, sv), cr = occ(su, sv);
                                    int ao = (s1 && s2) ? 0 : 3 - (s1 + s2 + cr);
                                    aoBits |= uint32_t(ao) << (2 * k);
                                }
                            }
                            key = uint32_t(c) | aoBits << 16 | 1u << 24;
                            any = true;
                        }
                    }
                    mask[j * N + i] = key;
                }
            if (!any) continue;

            int plane = s + (d.sign > 0 ? 1 : 0);
            for (int j = 0; j < N; ++j)
                for (int i = 0; i < N;) {
                    uint32_t key = mask[j * N + i];
                    if (!key) { ++i; continue; }
                    uint8_t ao[4];
                    uint32_t aoBits = (key >> 16) & 0xFF;
                    for (int k = 0; k < 4; ++k) ao[k] = uint8_t((aoBits >> (2 * k)) & 3);
                    bool uniform = ao[0] == ao[1] && ao[1] == ao[2] && ao[2] == ao[3];
                    int w = 1, h = 1;
                    if (greedy && uniform) {
                        while (i + w < N && mask[j * N + i + w] == key) ++w;
                        bool grow = true;
                        while (grow && j + h < N) {
                            for (int k = 0; k < w; ++k)
                                if (mask[(j + h) * N + i + k] != key) { grow = false; break; }
                            if (grow) ++h;
                        }
                    }
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x) mask[(j + y) * N + i + x] = 0;
                    uint16_t c = uint16_t(key & 0xFFFF);
                    const Material& m = ctx.model.materials[c];
                    auto& dst = kind[c] == kTransparent ? out.transparent : out.opaque;
                    emitQuad(dst, d, plane, i, j, w, h, ao, m, (m.flags & kMatEmissive) != 0, di);
                    i += w;
                }
        }
    }
}

}  // namespace

std::vector<ChunkMesh> buildMeshes(const VoxelModel& model, const MeshOptions& opts) {
    Context ctx{model, {}, {}};
    size_t n = model.materials.size();
    ctx.remap.resize(n);
    ctx.kind.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Material& m = model.materials[i];
        bool hidden = i == 0 || (opts.hideDecorations && (m.flags & kMatDecoration));
        ctx.remap[i] = hidden ? 0 : uint16_t(i);
        ctx.kind[i] = i == 0 ? kAir : (m.flags & kMatTransparent) ? kTransparent : kOpaque;
    }

    std::vector<IVec3> coords;
    for (const auto& kv : model.chunks())
        if (kv.second->count) coords.push_back(chunkCoordFromKey(kv.first));

    std::vector<ChunkMesh> meshes(coords.size());
    std::atomic<size_t> next{0};
    auto worker = [&] {
        std::vector<uint16_t> pad(size_t(P) * P * P);
        std::vector<uint32_t> mask(size_t(N) * N);
        for (;;) {
            size_t k = next.fetch_add(1);
            if (k >= coords.size()) break;
            meshChunk(ctx, coords[k], pad.data(), mask.data(), opts.greedy, meshes[k]);
        }
    };
    unsigned threads = std::max(1u, std::min(std::thread::hardware_concurrency(), unsigned(coords.size())));
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();

    // Drop chunks that produced no geometry (fully enclosed).
    std::vector<ChunkMesh> result;
    result.reserve(meshes.size());
    for (auto& m : meshes)
        if (!m.opaque.empty() || !m.transparent.empty()) result.push_back(std::move(m));
    return result;
}

}  // namespace vox
