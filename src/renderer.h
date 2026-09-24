#pragma once

#include <cstdint>
#include <vector>

#include "camera.h"
#include "gl_loader.h"
#include "mesher.h"
#include "smooth_mesher.h"

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
    void upload(const std::vector<SmoothChunkMesh>& meshes, IVec3 boundsMin, IVec3 boundsMax);
    void render(const Camera& cam, int width, int height, const RenderSettings& s);

    size_t drawnTriangles() const { return drawnTriangles_; }
    size_t totalTriangles() const { return totalTriangles_; }
    size_t gpuBytes() const { return gpuBytes_; }

private:
    struct GpuMesh {
        GLuint vao = 0, vbo = 0, ebo = 0;  // ebo is only owned by smooth meshes
        GLsizei indexCount = 0;
    };
    struct VoxelProgram {
        GLuint id = 0;
        GLint viewProj = -1, origin = -1, lightDir = -1, ao = -1, clipY = -1, eye = -1, fog = -1, fogColor = -1;
        bool init(const char* vs, const char* fs);
    };
    struct GpuChunk {
        Vec3 origin;
        GpuMesh opaque, transparent;
    };

    void freeChunks();
    GpuMesh makeMesh(const std::vector<PackedVertex>& verts);
    GpuMesh makeMesh(const SmoothMesh& mesh);
    void beginUpload(IVec3 boundsMin, IVec3 boundsMax);
    void buildGrid();

    VoxelProgram blockyProg_, smoothProg_;
    bool smooth_ = false;
    GLuint lineProg_ = 0, bgProg_ = 0;
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
    GLint uLineViewProj_ = -1, uLineColor_ = -1;
    GLint uBgTop_ = -1, uBgBottom_ = -1;
};

}  // namespace vox
