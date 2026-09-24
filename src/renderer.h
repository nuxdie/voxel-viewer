#pragma once

#include <cstdint>
#include <vector>

#include "camera.h"
#include "gl_loader.h"
#include "mesher.h"

namespace vox {

struct RenderSettings {
    bool wireframe = false;
    bool grid = true;
    bool bounds = false;
    float aoStrength = 1.0f;
    float clipY = 1e9f;  // world Y above which fragments are discarded
    bool darkBackground = true;
};

class Renderer {
public:
    bool init();
    void shutdown();
    // Replaces the GPU meshes. boundsMin/Max are inclusive voxel bounds.
    void upload(const std::vector<ChunkMesh>& meshes, IVec3 boundsMin, IVec3 boundsMax);
    void render(const Camera& cam, int width, int height, const RenderSettings& s);

    size_t drawnTriangles() const { return drawnTriangles_; }
    size_t totalTriangles() const { return totalTriangles_; }
    size_t gpuBytes() const { return gpuBytes_; }

private:
    struct GpuMesh {
        GLuint vao = 0, vbo = 0;
        GLsizei indexCount = 0;
    };
    struct GpuChunk {
        Vec3 origin;
        GpuMesh opaque, transparent;
    };

    void freeChunks();
    GpuMesh makeMesh(const std::vector<PackedVertex>& verts);
    void buildGrid();

    GLuint voxelProg_ = 0, lineProg_ = 0, bgProg_ = 0;
    GLuint ebo_ = 0;
    size_t eboQuads_ = 0;
    GLuint emptyVao_ = 0;
    GLuint lineVao_ = 0, lineVbo_ = 0;
    GLsizei gridVerts_ = 0, boundsVerts_ = 0;

    std::vector<GpuChunk> chunks_;
    IVec3 bmin_, bmax_;
    bool hasModel_ = false;
    size_t drawnTriangles_ = 0, totalTriangles_ = 0, gpuBytes_ = 0;

    // Uniform locations
    GLint uViewProj_ = -1, uOrigin_ = -1, uLightDir_ = -1, uAo_ = -1, uClipY_ = -1, uEye_ = -1, uFog_ = -1,
          uFogColor_ = -1;
    GLint uLineViewProj_ = -1, uLineColor_ = -1;
    GLint uBgTop_ = -1, uBgBottom_ = -1;
};

}  // namespace vox
