// Prints information about a voxel file without opening a window.
#include <chrono>
#include <cstdio>
#include <map>

#include "light_field.h"
#include "loader.h"
#include "mesher.h"
#include "smooth_mesher.h"
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: voxinfo [--smooth] <file>...\n");
        return 2;
    }
    int status = 0;
    bool smooth = false;
    for (int a = 1; a < argc; ++a) {
        if (std::strcmp(argv[a], "--smooth") == 0) {
            smooth = true;
            continue;
        }
        try {
            auto t0 = std::chrono::steady_clock::now();
            auto model = vox::loadModelFile(argv[a]);
            auto t1 = std::chrono::steady_clock::now();
            size_t quads = 0, tquads = 0, chunks = 0;
            if (smooth) {
                auto meshes = vox::buildSmoothMeshes(*model, {}, 8);
                chunks = meshes.size();
                for (const auto& m : meshes) {
                    quads += m.opaque.indices.size() / 6;
                    tquads += m.transparent.indices.size() / 6;
                }
            } else {
                auto meshes = vox::buildMeshes(*model, {});
                chunks = meshes.size();
                for (const auto& m : meshes) {
                    quads += m.opaque.size() / 4;
                    tquads += m.transparent.size() / 4;
                }
            }
            auto t2 = std::chrono::steady_clock::now();
            vox::IVec3 s = model->size(), mn = model->boundsMin();
            std::printf("%s\n  format:    %s\n  size:      %d x %d x %d (min %d,%d,%d)\n  voxels:    %zu\n"
                        "  materials: %zu\n  chunks:    %zu meshed\n  quads:     %zu opaque, %zu transparent\n"
                        "  time:      load %.1f ms, mesh %.1f ms\n",
                        argv[a], model->format.c_str(), s.x, s.y, s.z, mn.x, mn.y, mn.z, model->voxelCount(),
                        model->materials.size() - 1, chunks, quads, tquads,
                        std::chrono::duration<double, std::milli>(t1 - t0).count(),
                        std::chrono::duration<double, std::milli>(t2 - t1).count());
            if (model->light)
                std::printf("  lights:    %zu emitting voxels, %zu chunks lit\n", model->light->emitterCount(),
                            model->light->litChunkCount());
            // Most common materials.
            std::map<uint16_t, size_t> counts;
            for (const auto& kv : model->chunks())
                for (uint16_t v : kv.second->v)
                    if (v) ++counts[v];
            std::multimap<size_t, uint16_t, std::greater<size_t>> top;
            for (auto& kv : counts) top.emplace(kv.second, kv.first);
            int shown = 0;
            for (auto& kv : top) {
                if (shown++ == 8) break;
                const auto& m = model->materials[kv.second];
                std::printf("    %-40s #%02X%02X%02X  %zu\n", m.name.c_str(), m.color.r, m.color.g, m.color.b, kv.first);
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s: error: %s\n", argv[a], e.what());
            status = 1;
        }
    }
    return status;
}
