#include "smooth_mesher.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <unordered_map>

namespace vox {
namespace {

constexpr int N = kChunkSize;
constexpr float kCellMargin = 0.25f;  // minimum distance of a relaxed vertex from its cell's walls

enum Kind : uint8_t { kAir = 0, kOpaque = 1, kTransparent = 2 };

struct Context {
    const VoxelModel& model;
    std::vector<uint16_t> remap;  // palette index -> palette index (0 hides it)
    std::vector<uint8_t> kind;
    int iterations;
};

// Per-thread scratch buffers.
struct Work {
    int K = 0;       // relaxation iterations
    int pad = 0;     // voxel padding around the chunk
    int S = 0;       // voxel grid size (N + 2 * pad)
    int cmin = 0;    // first cell (min corner, chunk-local)
    int C = 0;       // cell grid size
    std::vector<uint16_t> vox;  // material per voxel, padded
    std::vector<int32_t> cellVert;
    std::vector<float> pos, next, nrm;
    std::vector<int32_t> vertCell;
    std::vector<int32_t> outIndex;  // work vertex -> emitted vertex
    std::vector<int32_t> srcOf;     // emitted vertex -> work vertex

    void init(int iterations) {
        K = iterations;
        pad = K + 2;
        S = N + 2 * pad;
        cmin = -1 - K;
        C = N + 1 + 2 * K;  // cells [-1-K, 31+K]
        vox.assign(size_t(S) * S * S, 0);
        cellVert.assign(size_t(C) * C * C, -1);
    }
    int vi(int x, int y, int z) const { return ((y + pad) * S + (z + pad)) * S + (x + pad); }
    int ci(int x, int y, int z) const { return ((y - cmin) * C + (z - cmin)) * C + (x - cmin); }
    bool cellInRange(int x, int y, int z) const {
        return x >= cmin && y >= cmin && z >= cmin && x < cmin + C && y < cmin + C && z < cmin + C;
    }
};

void fillVoxels(const Context& ctx, IVec3 cc, Work& w) {
    for (int dy = -1; dy <= 1; ++dy)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const Chunk* c = ctx.model.chunk(cc.x + dx, cc.y + dy, cc.z + dz);
                auto range = [&](int d, int& a, int& b) {
                    a = d < 0 ? -w.pad : d > 0 ? N : 0;
                    b = d < 0 ? -1 : d > 0 ? N + w.pad - 1 : N - 1;
                };
                int x0, x1, y0, y1, z0, z1;
                range(dx, x0, x1);
                range(dy, y0, y1);
                range(dz, z0, z1);
                for (int y = y0; y <= y1; ++y)
                    for (int z = z0; z <= z1; ++z)
                        for (int x = x0; x <= x1; ++x) {
                            uint16_t v = 0;
                            if (c) v = ctx.remap[c->v[Chunk::index(x & kChunkMask, y & kChunkMask, z & kChunkMask)]];
                            w.vox[size_t(w.vi(x, y, z))] = v;
                        }
            }
}

// Tangent axes per edge axis, with u x v = axis.
constexpr int kU[3] = {1, 2, 0};
constexpr int kV[3] = {2, 0, 1};

void meshLayer(const Context& ctx, Work& w, uint8_t layer, SmoothMesh& out) {
    auto solid = [&](int x, int y, int z) { return ctx.kind[w.vox[size_t(w.vi(x, y, z))]] == layer; };

    // 1. One vertex per cell that straddles the surface, at the mean of its edge midpoints.
    std::fill(w.cellVert.begin(), w.cellVert.end(), -1);
    w.pos.clear();
    w.vertCell.clear();
    const int lo = w.cmin, hi = w.cmin + w.C;
    for (int y = lo; y < hi; ++y)
        for (int z = lo; z < hi; ++z)
            for (int x = lo; x < hi; ++x) {
                int mask = 0;
                for (int k = 0; k < 8; ++k)
                    if (solid(x + (k & 1), y + ((k >> 1) & 1), z + ((k >> 2) & 1))) mask |= 1 << k;
                if (mask == 0 || mask == 255) continue;
                float sx = 0, sy = 0, sz = 0;
                int n = 0;
                for (int k = 0; k < 8; ++k)
                    for (int bit = 1; bit < 8; bit <<= 1) {
                        int j = k | bit;
                        if (j == k) continue;
                        if (((mask >> k) & 1) == ((mask >> j) & 1)) continue;
                        sx += float((k & 1) + (j & 1)) * 0.5f;
                        sy += float(((k >> 1) & 1) + ((j >> 1) & 1)) * 0.5f;
                        sz += float(((k >> 2) & 1) + ((j >> 2) & 1)) * 0.5f;
                        ++n;
                    }
                w.cellVert[size_t(w.ci(x, y, z))] = int32_t(w.pos.size() / 3);
                w.pos.insert(w.pos.end(), {float(x) + sx / float(n), float(y) + sy / float(n), float(z) + sz / float(n)});
                w.vertCell.insert(w.vertCell.end(), {x, y, z});
            }
    size_t nv = w.pos.size() / 3;
    if (nv == 0) return;

    // 2. Relax: move toward the mean of face-adjacent cell vertices, clamped to the own cell.
    static const int kNb[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    w.next.resize(w.pos.size());
    for (int it = 0; it < w.K; ++it) {
        for (size_t v = 0; v < nv; ++v) {
            const int* c = &w.vertCell[v * 3];
            float ax = 0, ay = 0, az = 0;
            int n = 0;
            for (const auto& d : kNb) {
                int x = c[0] + d[0], y = c[1] + d[1], z = c[2] + d[2];
                if (!w.cellInRange(x, y, z)) continue;
                int32_t o = w.cellVert[size_t(w.ci(x, y, z))];
                if (o < 0) continue;
                ax += w.pos[size_t(o) * 3];
                ay += w.pos[size_t(o) * 3 + 1];
                az += w.pos[size_t(o) * 3 + 2];
                ++n;
            }
            float* p = &w.next[v * 3];
            if (n == 0) {
                std::copy_n(&w.pos[v * 3], 3, p);
                continue;
            }
            // Keep a margin inside the cell: without it, one-voxel pillars and gaps collapse
            // to zero thickness as their vertices slide onto the voxel centers.
            constexpr float m = kCellMargin;
            p[0] = std::clamp(ax / float(n), float(c[0]) + m, float(c[0] + 1) - m);
            p[1] = std::clamp(ay / float(n), float(c[1]) + m, float(c[1] + 1) - m);
            p[2] = std::clamp(az / float(n), float(c[2]) + m, float(c[2] + 1) - m);
        }
        std::swap(w.pos, w.next);
    }

    // 3. Quads for every sign-changing edge between voxel centers. Normals accumulate over the
    //    whole padded region so they match across chunk borders; only edges whose lower voxel
    //    lies in this chunk are emitted.
    w.nrm.assign(w.pos.size(), 0.0f);
    w.outIndex.assign(nv, -1);
    w.srcOf.clear();
    auto vpos = [&](int32_t v) { return &w.pos[size_t(v) * 3]; };
    auto emitVertex = [&](int32_t v) -> uint32_t {
        if (w.outIndex[size_t(v)] < 0) {
            w.outIndex[size_t(v)] = int32_t(out.vertices.size());
            out.vertices.push_back(SmoothVertex{});
            w.srcOf.push_back(v);
        }
        return uint32_t(w.outIndex[size_t(v)]);
    };
    for (int y = lo + 1; y < hi; ++y)
        for (int z = lo + 1; z < hi; ++z)
            for (int x = lo + 1; x < hi; ++x) {
                int p[3] = {x, y, z};
                bool s0 = solid(x, y, z);
                for (int a = 0; a < 3; ++a) {
                    int q[3] = {x, y, z};
                    q[a] += 1;  // may be hi: voxels are padded past the last cell
                    bool s1 = solid(q[0], q[1], q[2]);
                    if (s0 == s1) continue;
                    int u = kU[a], v = kV[a];
                    int cells[4][3];
                    static const int du[4] = {-1, 0, 0, -1}, dv[4] = {-1, -1, 0, 0};
                    int32_t ids[4];
                    bool ok = true;
                    for (int k = 0; k < 4; ++k) {
                        std::copy_n(p, 3, cells[k]);
                        cells[k][u] += du[k];
                        cells[k][v] += dv[k];
                        ids[k] = w.cellVert[size_t(w.ci(cells[k][0], cells[k][1], cells[k][2]))];
                        if (ids[k] < 0) ok = false;
                    }
                    if (!ok) continue;
                    if (!s0) std::swap(ids[1], ids[3]);  // flip winding: surface faces -a
                    // Split along the shorter diagonal.
                    auto dist2 = [&](int32_t i, int32_t j) {
                        const float *A = vpos(i), *B = vpos(j);
                        float dx = A[0] - B[0], dy = A[1] - B[1], dz = A[2] - B[2];
                        return dx * dx + dy * dy + dz * dz;
                    };
                    int32_t tris[6];
                    if (dist2(ids[0], ids[2]) <= dist2(ids[1], ids[3])) {
                        int32_t t[6] = {ids[0], ids[1], ids[2], ids[0], ids[2], ids[3]};
                        std::copy_n(t, 6, tris);
                    } else {
                        int32_t t[6] = {ids[0], ids[1], ids[3], ids[1], ids[2], ids[3]};
                        std::copy_n(t, 6, tris);
                    }
                    for (int t = 0; t < 6; t += 3) {
                        const float *A = vpos(tris[t]), *B = vpos(tris[t + 1]), *Cp = vpos(tris[t + 2]);
                        float e1[3] = {B[0] - A[0], B[1] - A[1], B[2] - A[2]};
                        float e2[3] = {Cp[0] - A[0], Cp[1] - A[1], Cp[2] - A[2]};
                        float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                        for (int k = 0; k < 3; ++k)
                            for (int c = 0; c < 3; ++c) w.nrm[size_t(tris[t + k]) * 3 + size_t(c)] += n[c];
                    }
                    bool owned = x >= 0 && y >= 0 && z >= 0 && x < N && y < N && z < N;
                    if (owned)
                        for (int32_t t : tris) out.indices.push_back(emitVertex(t));
                }
            }

    // 4. Fill in the emitted vertices: position, normal, color and ambient occlusion.
    for (size_t oi = 0; oi < out.vertices.size(); ++oi) {
        SmoothVertex& sv = out.vertices[oi];
        int32_t v = w.srcOf[oi];
        const float* p = vpos(v);
        const int* c = &w.vertCell[size_t(v) * 3];
        // Voxel centers sit at +0.5; store with a +2 offset in 1/1024 units.
        sv.x = uint16_t(std::lround((p[0] + 0.5f + 2.0f) * 1024.0f));
        sv.y = uint16_t(std::lround((p[1] + 0.5f + 2.0f) * 1024.0f));
        sv.z = uint16_t(std::lround((p[2] + 0.5f + 2.0f) * 1024.0f));
        float* n = &w.nrm[size_t(v) * 3];
        float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 0) for (int k = 0; k < 3; ++k) n[k] /= len;
        sv.nx = int8_t(std::lround(n[0] * 127.0f));
        sv.ny = int8_t(std::lround(n[1] * 127.0f));
        sv.nz = int8_t(std::lround(n[2] * 127.0f));

        // Color and emissive flag from the nearest solid corner voxel; averaging all corners
        // blurs small pixel-art details too much.
        uint16_t best = 0;
        float bestD = 1e9f;
        for (int k = 0; k < 8; ++k) {
            int x = c[0] + (k & 1), y = c[1] + ((k >> 1) & 1), z = c[2] + ((k >> 2) & 1);
            uint16_t m = w.vox[size_t(w.vi(x, y, z))];
            if (ctx.kind[m] != layer) continue;
            float dx = p[0] - float(x), dy = p[1] - float(y), dz = p[2] - float(z);
            float d = dx * dx + dy * dy + dz * dz;
            if (d < bestD) { bestD = d; best = m; }
        }
        const Material& mat = ctx.model.materials[best];
        sv.r = mat.color.r;
        sv.g = mat.color.g;
        sv.b = mat.color.b;
        sv.a = layer == kTransparent ? mat.color.a : 255;
        bool emissive = (mat.flags & kMatEmissive) != 0;

        // Ambient occlusion: how enclosed the vertex is by opaque voxels in a 4x4x4 neighborhood.
        int occ = 0;
        for (int dy = -1; dy <= 2; ++dy)
            for (int dz = -1; dz <= 2; ++dz)
                for (int dx = -1; dx <= 2; ++dx)
                    occ += ctx.kind[w.vox[size_t(w.vi(c[0] + dx, c[1] + dy, c[2] + dz))]] == kOpaque;
        float frac = float(occ) / 64.0f;
        float light = std::clamp(1.0f - (frac - 0.42f) * 1.8f, 0.3f, 1.0f);
        sv.aoEmissive = uint8_t(std::lround(light * 127.0f)) | (emissive ? 0x80 : 0);
    }
}

}  // namespace

std::vector<SmoothChunkMesh> buildSmoothMeshes(const VoxelModel& model, const MeshOptions& opts, int iterations) {
    Context ctx{model, {}, {}, std::clamp(iterations, 0, kMaxSmoothIterations)};
    size_t n = model.materials.size();
    ctx.remap.resize(n);
    ctx.kind.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Material& m = model.materials[i];
        bool hidden = i == 0 || (opts.hideDecorations && (m.flags & kMatDecoration));
        ctx.remap[i] = hidden ? 0 : uint16_t(i);
        ctx.kind[i] = hidden ? kAir : (m.flags & kMatTransparent) ? kTransparent : kOpaque;
    }

    // A chunk's surface can touch voxels in the next chunk, so mesh empty neighbors too.
    std::vector<IVec3> coords;
    {
        std::unordered_map<uint64_t, bool> seen;
        for (const auto& kv : model.chunks()) {
            if (!kv.second->count) continue;
            IVec3 c = chunkCoordFromKey(kv.first);
            for (int dy = -1; dy <= 0; ++dy)
                for (int dz = -1; dz <= 0; ++dz)
                    for (int dx = -1; dx <= 0; ++dx) {
                        uint64_t k = chunkKey(c.x + dx, c.y + dy, c.z + dz);
                        if (seen.emplace(k, true).second) coords.push_back({c.x + dx, c.y + dy, c.z + dz});
                    }
        }
    }

    std::vector<SmoothChunkMesh> meshes(coords.size());
    std::atomic<size_t> next{0};
    auto worker = [&] {
        Work w;
        w.init(ctx.iterations);
        for (;;) {
            size_t k = next.fetch_add(1);
            if (k >= coords.size()) break;
            SmoothChunkMesh& m = meshes[k];
            m.chunk = coords[k];
            fillVoxels(ctx, coords[k], w);
            bool any[3] = {false, false, false};
            for (uint16_t v : w.vox) any[ctx.kind[v]] = true;
            if (any[kOpaque]) meshLayer(ctx, w, kOpaque, m.opaque);
            if (any[kTransparent]) meshLayer(ctx, w, kTransparent, m.transparent);
        }
    };
    unsigned threads = std::max(1u, std::min(std::thread::hardware_concurrency(), unsigned(coords.size())));
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();

    std::vector<SmoothChunkMesh> result;
    for (auto& m : meshes)
        if (!m.opaque.indices.empty() || !m.transparent.indices.empty()) result.push_back(std::move(m));
    return result;
}

}  // namespace vox
