#pragma once

#include <cstdint>
#include <vector>

#include "camera.h"
#include "gl_loader.h"
#include "mesher.h"
#include "smooth_mesher.h"
#include "splats.h"

namespace vox {

struct RenderSettings {
    bool wireframe = false;
    bool grid = true;
    bool bounds = false;
    float aoStrength = 1.0f;
    float clipY = 1e9f;  // world Y above which fragments are discarded
    bool darkBackground = false;  // dark studio backdrop instead of the sky
    bool shadows = true;
    bool night = false;           // moonlight: dim blue sun and sky, so block light stands out
    int paintStyle = 0;  // splat modes: 0 = painted brush dabs, 1 = watercolor on paper
};

class Renderer {
public:
    bool init();
    void shutdown();
    // Replaces the GPU meshes. boundsMin/Max are inclusive voxel bounds.
    void upload(const std::vector<ChunkMesh>& meshes, IVec3 boundsMin, IVec3 boundsMax);
    void upload(const std::vector<SmoothChunkMesh>& meshes, IVec3 boundsMin, IVec3 boundsMax);
    // Watercolor mode: soft pigment splats + a painterly post-process.
    void upload(const std::vector<SplatChunk>& splats, IVec3 boundsMin, IVec3 boundsMax);
    void render(const Camera& cam, int width, int height, const RenderSettings& s);

    size_t drawnTriangles() const { return drawnTriangles_; }
    size_t totalTriangles() const { return totalTriangles_; }
    size_t gpuBytes() const { return gpuBytes_; }

private:
    struct GpuMesh {
        GLuint vao = 0, vbo = 0, ebo = 0;  // ebo is only owned by smooth meshes
        GLsizei indexCount = 0;  // splat meshes: instance count
    };
    // Uniforms of the shared lighting code (kLightingGlsl) in one program.
    struct LightLocs {
        GLint lightDir = -1, lightVP = -1, shadowMap = -1, shadowTexel = -1, shadowsOn = -1, eye = -1, fogDist = -1,
              horizon = -1, ao = -1, sunColor = -1, skyColor = -1, bounceColor = -1, skyTop = -1;
        void init(GLuint prog);
    };
    struct VoxelProgram {
        GLuint id = 0;
        GLint viewProj = -1, origin = -1, clipY = -1;
        LightLocs light;
        bool init(const char* vs, const char* fs);
    };
    struct ShadowMeshProgram {
        GLuint id = 0;
        GLint viewProj = -1, origin = -1, clipY = -1;
        bool init(const char* vs, const char* fs);
    };
    // Per-frame lighting environment shared by every mode.
    struct Environment {
        Vec3 skyTop, skyBottom, horizonLinear, skyTopLinear;
        Vec3 sunColor, skyColor, bounceColor;  // linear
    };
    struct GpuChunk {
        Vec3 origin;
        GpuMesh opaque, transparent;
    };

    void freeChunks();
    GpuMesh makeMesh(const std::vector<PackedVertex>& verts);
    GpuMesh makeMesh(const SmoothMesh& mesh);
    GpuMesh makeMesh(const std::vector<Splat>& splats);
    void renderWatercolor(const Camera& cam, int width, int height, const RenderSettings& s, const Environment& env);
    Environment environment(const RenderSettings& s) const;
    void applyLighting(const LightLocs& l, const Camera& cam, const RenderSettings& s, const Environment& env);
    void drawSky(const Environment& env);
    bool ensureFbo(int width, int height);
    void renderShadowMap(float clipY);
    void beginUpload(IVec3 boundsMin, IVec3 boundsMax);
    void buildGrid();

    VoxelProgram blockyProg_, smoothProg_;
    enum class Mode { Blocky, Smooth, Watercolor } mode_ = Mode::Blocky;

    // Watercolor pipeline
    GLuint splatProg_ = 0, paintProg_ = 0;
    GLint uSplatView_ = -1, uSplatProj_ = -1, uSplatOrigin_ = -1, uSplatClipY_ = -1, uSplatSize_ = -1;
    LightLocs splatLight_;
    ShadowMeshProgram shadowBlocky_, shadowSmooth_;
    GLint uPaintColor_ = -1, uPaintDepth_ = -1, uPaintTexel_ = -1;
    GLuint fbo_ = 0, fboColor_ = 0, fboDepth_ = 0;
    int fboW_ = 0, fboH_ = 0;

    // Painted style: sun shadow map, lit brush dabs, Kuwahara post-process.
    struct PaintedLocs {
        GLint view = -1, proj = -1, origin = -1, clipY = -1, size = -1;
        LightLocs light;
    } pl_;
    GLuint paintedProg_ = 0, kuwaharaProg_ = 0, shadowProg_ = 0;
    GLint uKuwColor_ = -1, uKuwTexel_ = -1;
    GLint uShView_ = -1, uShProj_ = -1, uShOrigin_ = -1, uShClipY_ = -1, uShSize_ = -1;
    GLuint shadowFbo_ = 0, shadowTex_ = 0;
    static constexpr int kShadowSize = 2048;
    bool shadowDirty_ = true;
    float shadowClip_ = 0;
    Mat4 lightVP_;
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
