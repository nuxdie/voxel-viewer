// Checks that the smooth mesher produces a closed, consistently wound surface across chunk
// borders: every directed edge must be matched by exactly one opposite edge.
#include <cstdio>
#include <map>
#include <tuple>

#include "smooth_mesher.h"
#include "splats.h"

using namespace vox;

static int checkClosed(const VoxelModel& model, int iterations, const char* name) {
    auto meshes = buildSmoothMeshes(model, {}, iterations);
    using P = std::tuple<long, long, long>;
    std::map<std::pair<P, P>, int> edges;
    size_t tris = 0;
    for (const auto& m : meshes) {
        const SmoothMesh& sm = m.opaque;
        auto world = [&](uint32_t i) {
            const SmoothVertex& v = sm.vertices[i];
            return P{long(m.chunk.x) * kChunkSize * 1024 + v.x, long(m.chunk.y) * kChunkSize * 1024 + v.y,
                     long(m.chunk.z) * kChunkSize * 1024 + v.z};
        };
        for (size_t t = 0; t + 2 < sm.indices.size(); t += 3) {
            P a = world(sm.indices[t]), b = world(sm.indices[t + 1]), c = world(sm.indices[t + 2]);
            ++edges[{a, b}];
            ++edges[{b, c}];
            ++edges[{c, a}];
            ++tris;
        }
    }
    size_t bad = 0;
    for (const auto& e : edges) {
        auto it = edges.find({e.first.second, e.first.first});
        if (e.second != 1 || it == edges.end() || it->second != 1) ++bad;
    }
    std::printf("%-28s iterations=%-2d triangles=%-7zu unmatched edges=%zu\n", name, iterations, tris, bad);
    return bad == 0 && tris > 0 ? 0 : 1;
}

int main() {
    int failures = 0;

    // A sphere straddling chunk borders on all axes (including negative coordinates).
    VoxelModel sphere;
    sphere.materials.push_back(Material{{200, 100, 50, 255}, 0, "a"});
    sphere.materials.push_back(Material{{50, 100, 200, 255}, 0, "b"});
    for (int z = -20; z <= 20; ++z)
        for (int y = -20; y <= 20; ++y)
            for (int x = -20; x <= 20; ++x)
                if (x * x + y * y + z * z <= 18 * 18) sphere.set(x + 3, y - 5, z + 30, x > 0 ? 1 : 2);

    // Thin features: a one-voxel pillar and a wall with a one-voxel hole, across a chunk border.
    VoxelModel thin;
    thin.materials.push_back(Material{{128, 128, 128, 255}, 0, "stone"});
    for (int y = 0; y < 40; ++y) thin.set(30, y, 30, 1);
    for (int y = 0; y < 10; ++y)
        for (int x = 25; x < 40; ++x)
            if (!(x == 32 && y == 5)) thin.set(x, y, 40, 1);

    for (int it : {0, 8, 16}) {
        failures += checkClosed(sphere, it, "sphere across chunks");
        failures += checkClosed(thin, it, "pillar + wall with hole");
    }
    // Watercolor splats: one per surface voxel of a cube spanning chunks, normals pointing out.
    VoxelModel cube;
    cube.materials.push_back(Material{{90, 140, 200, 255}, 0, "c"});
    for (int z = 28; z < 38; ++z)
        for (int y = -5; y < 5; ++y)
            for (int x = 28; x < 38; ++x) cube.set(x, y, z, 1);
    size_t splats = 0, inward = 0;
    for (const auto& sc : buildSplats(cube, {})) {
        for (const Splat& sp : sc.splats) {
            ++splats;
            float px = float(sc.chunk.x * kChunkSize + sp.x) + 0.5f - 33.0f;
            float py = float(sc.chunk.y * kChunkSize + sp.y) + 0.5f - 0.0f;
            float pz = float(sc.chunk.z * kChunkSize + sp.z) + 0.5f - 33.0f;
            if (px * sp.nx + py * sp.ny + pz * sp.nz <= 0) ++inward;
        }
    }
    bool splatsOk = splats == 10 * 10 * 10 - 8 * 8 * 8 && inward == 0;
    std::printf("%-28s splats=%zu (expected 488) inward normals=%zu\n", "cube splats", splats, inward);
    failures += splatsOk ? 0 : 1;

    std::printf(failures ? "FAILED\n" : "all smooth mesh and splat checks passed\n");
    return failures ? 1 : 0;
}
