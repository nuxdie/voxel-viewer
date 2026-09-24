#include "renderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>

namespace vox {
namespace {

const char* kVoxelVs = R"(#version 330 core
layout(location = 0) in uvec4 aPos;   // x, y, z (chunk local), normal | ao << 3 | emissive << 5
layout(location = 1) in vec4 aColor;
uniform mat4 uViewProj;
uniform vec3 uOrigin;
out vec4 vColor;
out vec3 vWorld;
out vec3 vNormal;
out float vAo;
flat out int vEmissive;
const vec3 kNormals[6] = vec3[6](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0),
                                 vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1));
void main() {
    vec3 p = vec3(aPos.xyz) + uOrigin;
    vWorld = p;
    vColor = aColor;
    vNormal = kNormals[int(aPos.w & 7u)];
    vAo = pow(float((aPos.w >> 3) & 3u) / 3.0, 1.3) * 0.75 + 0.25;
    vEmissive = int((aPos.w >> 5) & 1u);
    gl_Position = uViewProj * vec4(p, 1.0);
}
)";

const char* kSmoothVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;      // chunk-local fixed point: p / 1024 - 2
layout(location = 1) in vec3 aNormal;   // normalized snorm8
layout(location = 2) in uint aAoEmissive;
layout(location = 3) in vec4 aColor;
uniform mat4 uViewProj;
uniform vec3 uOrigin;
out vec4 vColor;
out vec3 vWorld;
out vec3 vNormal;
out float vAo;
flat out int vEmissive;
void main() {
    vec3 p = aPos / 1024.0 - 2.0 + uOrigin;
    vWorld = p;
    vColor = aColor;
    vNormal = aNormal;
    vAo = float(aAoEmissive & 127u) / 127.0;
    vEmissive = int(aAoEmissive >> 7);
    gl_Position = uViewProj * vec4(p, 1.0);
}
)";

const char* kVoxelFs = R"(#version 330 core
in vec4 vColor;
in vec3 vWorld;
in vec3 vNormal;
in float vAo;
flat in int vEmissive;
uniform vec3 uLightDir;
uniform float uAoStrength;
uniform float uClipY;
uniform vec3 uEye;
uniform float uFog;        // 1 / fog distance
uniform vec3 uFogColor;
out vec4 fragColor;
void main() {
    if (vWorld.y > uClipY) discard;
    vec3 base = pow(vColor.rgb, vec3(2.2));
    vec3 lit;
    if (vEmissive != 0) {
        lit = base * 1.15;
    } else {
        vec3 n = normalize(vNormal);
        if (!gl_FrontFacing) n = -n;
        float sun = max(dot(n, uLightDir), 0.0);
        float sky = 0.5 + 0.5 * n.y;
        float ao = mix(1.0, vAo, uAoStrength);
        vec3 light = vec3(1.0, 0.97, 0.9) * sun * 0.75 + vec3(0.62, 0.68, 0.8) * (0.25 + 0.35 * sky);
        lit = base * light * ao;
    }
    float dist = length(vWorld - uEye);
    float fog = clamp(1.0 - exp(-pow(dist * uFog, 2.0)), 0.0, 0.6);
    vec3 c = pow(lit, vec3(1.0 / 2.2));
    fragColor = vec4(mix(c, uFogColor, fog), vColor.a);
}
)";

const char* kLineVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
void main() { gl_Position = uViewProj * vec4(aPos, 1.0); }
)";

const char* kLineFs = R"(#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() { fragColor = uColor; }
)";

const char* kBgVs = R"(#version 330 core
out float vY;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vY = p.y * 0.5 + 0.5;
    gl_Position = vec4(p, 0.999, 1.0);
}
)";

const char* kBgFs = R"(#version 330 core
in float vY;
uniform vec3 uTop;
uniform vec3 uBottom;
out vec4 fragColor;
void main() { fragColor = vec4(mix(uBottom, uTop, vY), 1.0); }
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader compile error:\n%s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(const char* vs, const char* fs) {
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader link error:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// Frustum planes (a, b, c, d) extracted from a view-projection matrix.
std::array<std::array<float, 4>, 6> frustumPlanes(const Mat4& m) {
    std::array<std::array<float, 4>, 6> p;
    for (int i = 0; i < 3; ++i) {
        for (int k = 0; k < 4; ++k) {
            p[size_t(i * 2)][size_t(k)] = m(3, k) + m(i, k);
            p[size_t(i * 2 + 1)][size_t(k)] = m(3, k) - m(i, k);
        }
    }
    return p;
}

bool boxVisible(const std::array<std::array<float, 4>, 6>& planes, const Vec3& mn, const Vec3& mx) {
    for (const auto& pl : planes) {
        Vec3 v(pl[0] >= 0 ? mx.x : mn.x, pl[1] >= 0 ? mx.y : mn.y, pl[2] >= 0 ? mx.z : mn.z);
        if (pl[0] * v.x + pl[1] * v.y + pl[2] * v.z + pl[3] < 0) return false;
    }
    return true;
}

}  // namespace

bool Renderer::VoxelProgram::init(const char* vs, const char* fs) {
    id = link(vs, fs);
    if (!id) return false;
    viewProj = glGetUniformLocation(id, "uViewProj");
    origin = glGetUniformLocation(id, "uOrigin");
    lightDir = glGetUniformLocation(id, "uLightDir");
    ao = glGetUniformLocation(id, "uAoStrength");
    clipY = glGetUniformLocation(id, "uClipY");
    eye = glGetUniformLocation(id, "uEye");
    fog = glGetUniformLocation(id, "uFog");
    fogColor = glGetUniformLocation(id, "uFogColor");
    return true;
}

bool Renderer::init() {
    lineProg_ = link(kLineVs, kLineFs);
    bgProg_ = link(kBgVs, kBgFs);
    if (!blockyProg_.init(kVoxelVs, kVoxelFs) || !smoothProg_.init(kSmoothVs, kVoxelFs) || !lineProg_ || !bgProg_)
        return false;

    uLineViewProj_ = glGetUniformLocation(lineProg_, "uViewProj");
    uLineColor_ = glGetUniformLocation(lineProg_, "uColor");
    uBgTop_ = glGetUniformLocation(bgProg_, "uTop");
    uBgBottom_ = glGetUniformLocation(bgProg_, "uBottom");

    glGenVertexArrays(1, &emptyVao_);
    glGenBuffers(1, &ebo_);
    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);
    glBindVertexArray(0);
    return true;
}

void Renderer::freeChunks() {
    for (auto& c : chunks_)
        for (GpuMesh* m : {&c.opaque, &c.transparent}) {
            if (m->vbo) glDeleteBuffers(1, &m->vbo);
            if (m->ebo) glDeleteBuffers(1, &m->ebo);
            if (m->vao) glDeleteVertexArrays(1, &m->vao);
        }
    chunks_.clear();
    gpuBytes_ = 0;
    totalTriangles_ = 0;
}

void Renderer::shutdown() {
    freeChunks();
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (lineVbo_) glDeleteBuffers(1, &lineVbo_);
    if (lineVao_) glDeleteVertexArrays(1, &lineVao_);
    if (emptyVao_) glDeleteVertexArrays(1, &emptyVao_);
    for (GLuint p : {blockyProg_.id, smoothProg_.id, lineProg_, bgProg_})
        if (p) glDeleteProgram(p);
}

Renderer::GpuMesh Renderer::makeMesh(const std::vector<PackedVertex>& verts) {
    GpuMesh m;
    if (verts.empty()) return m;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(PackedVertex)), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribIPointer(0, 4, GL_UNSIGNED_BYTE, sizeof(PackedVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PackedVertex), reinterpret_cast<void*>(4));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBindVertexArray(0);
    m.indexCount = GLsizei(verts.size() / 4 * 6);
    gpuBytes_ += verts.size() * sizeof(PackedVertex);
    totalTriangles_ += verts.size() / 2;
    return m;
}

void Renderer::beginUpload(IVec3 bmin, IVec3 bmax) {
    freeChunks();
    bmin_ = bmin;
    bmax_ = bmax;
    hasModel_ = true;
}

void Renderer::upload(const std::vector<ChunkMesh>& meshes, IVec3 bmin, IVec3 bmax) {
    beginUpload(bmin, bmax);
    smooth_ = false;

    // Shared quad index buffer, large enough for the biggest chunk mesh.
    size_t maxQuads = 1;
    for (const auto& m : meshes) maxQuads = std::max({maxQuads, m.opaque.size() / 4, m.transparent.size() / 4});
    if (maxQuads > eboQuads_) {
        std::vector<uint32_t> idx(maxQuads * 6);
        for (size_t q = 0; q < maxQuads; ++q) {
            uint32_t b = uint32_t(q * 4);
            uint32_t* o = &idx[q * 6];
            o[0] = b; o[1] = b + 1; o[2] = b + 2; o[3] = b; o[4] = b + 2; o[5] = b + 3;
        }
        glBindVertexArray(0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * sizeof(uint32_t)), idx.data(), GL_STATIC_DRAW);
        eboQuads_ = maxQuads;
    }

    chunks_.reserve(meshes.size());
    for (const auto& m : meshes) {
        GpuChunk c;
        c.origin = Vec3(float(m.chunk.x * kChunkSize), float(m.chunk.y * kChunkSize), float(m.chunk.z * kChunkSize));
        c.opaque = makeMesh(m.opaque);
        c.transparent = makeMesh(m.transparent);
        chunks_.push_back(c);
    }
    buildGrid();
}

void Renderer::upload(const std::vector<SmoothChunkMesh>& meshes, IVec3 bmin, IVec3 bmax) {
    beginUpload(bmin, bmax);
    smooth_ = true;
    chunks_.reserve(meshes.size());
    for (const auto& m : meshes) {
        GpuChunk c;
        c.origin = Vec3(float(m.chunk.x * kChunkSize), float(m.chunk.y * kChunkSize), float(m.chunk.z * kChunkSize));
        c.opaque = makeMesh(m.opaque);
        c.transparent = makeMesh(m.transparent);
        chunks_.push_back(c);
    }
    buildGrid();
}

Renderer::GpuMesh Renderer::makeMesh(const SmoothMesh& mesh) {
    GpuMesh m;
    if (mesh.indices.empty()) return m;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(mesh.vertices.size() * sizeof(SmoothVertex)), mesh.vertices.data(), GL_STATIC_DRAW);
    const GLsizei stride = sizeof(SmoothVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_UNSIGNED_SHORT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(SmoothVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(SmoothVertex, nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, stride, reinterpret_cast<void*>(offsetof(SmoothVertex, aoEmissive)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(SmoothVertex, r)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(mesh.indices.size() * sizeof(uint32_t)), mesh.indices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    m.indexCount = GLsizei(mesh.indices.size());
    gpuBytes_ += mesh.vertices.size() * sizeof(SmoothVertex) + mesh.indices.size() * sizeof(uint32_t);
    totalTriangles_ += mesh.indices.size() / 3;
    return m;
}

void Renderer::buildGrid() {
    std::vector<float> v;
    auto line = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
        v.insert(v.end(), {x0, y0, z0, x1, y1, z1});
    };
    // Floor grid under the model; 1-voxel spacing for small models, chunk spacing for large ones.
    int extent = std::max(bmax_.x - bmin_.x, bmax_.z - bmin_.z) + 1;
    int step = extent <= 160 ? 1 : extent <= 1024 ? 16 : 64;
    while (extent / step > 2048) step *= 4;  // keep the line count bounded for huge extents
    int margin = std::max(step * 4, extent / 8);
    int x0 = (bmin_.x - margin) / step * step, x1 = (bmax_.x + 1 + margin) / step * step;
    int z0 = (bmin_.z - margin) / step * step, z1 = (bmax_.z + 1 + margin) / step * step;
    float y = float(bmin_.y) - 0.002f;
    for (int x = x0; x <= x1; x += step) line(float(x), y, float(z0), float(x), y, float(z1));
    for (int z = z0; z <= z1; z += step) line(float(x0), y, float(z), float(x1), y, float(z));
    gridVerts_ = GLsizei(v.size() / 3);
    // Bounding box
    float ax = float(bmin_.x), ay = float(bmin_.y), az = float(bmin_.z);
    float bx = float(bmax_.x + 1), by = float(bmax_.y + 1), bz = float(bmax_.z + 1);
    line(ax, ay, az, bx, ay, az); line(ax, by, az, bx, by, az); line(ax, ay, bz, bx, ay, bz); line(ax, by, bz, bx, by, bz);
    line(ax, ay, az, ax, by, az); line(bx, ay, az, bx, by, az); line(ax, ay, bz, ax, by, bz); line(bx, ay, bz, bx, by, bz);
    line(ax, ay, az, ax, ay, bz); line(bx, ay, az, bx, ay, bz); line(ax, by, az, ax, by, bz); line(bx, by, az, bx, by, bz);
    boundsVerts_ = 24;
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
}

void Renderer::render(const Camera& cam, int width, int height, const RenderSettings& s) {
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    Vec3 bgTop = s.darkBackground ? Vec3(0.20f, 0.22f, 0.27f) : Vec3(0.78f, 0.84f, 0.92f);
    Vec3 bgBottom = s.darkBackground ? Vec3(0.07f, 0.08f, 0.10f) : Vec3(0.95f, 0.95f, 0.96f);

    // Background gradient
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(bgProg_);
    glUniform3f(uBgTop_, bgTop.x, bgTop.y, bgTop.z);
    glUniform3f(uBgBottom_, bgBottom.x, bgBottom.y, bgBottom.z);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    drawnTriangles_ = 0;
    if (!hasModel_) return;

    float aspect = float(width) / float(std::max(1, height));
    Mat4 vp = cam.projection(aspect) * cam.view();
    auto planes = frustumPlanes(vp);
    Vec3 eye = cam.eye();

    const VoxelProgram& prog = smooth_ ? smoothProg_ : blockyProg_;
    glUseProgram(prog.id);
    glUniformMatrix4fv(prog.viewProj, 1, GL_FALSE, vp.m);
    Vec3 light = normalize(Vec3(0.45f, 0.85f, 0.3f));
    glUniform3f(prog.lightDir, light.x, light.y, light.z);
    glUniform1f(prog.ao, s.aoStrength);
    glUniform1f(prog.clipY, s.clipY);
    glUniform3f(prog.eye, eye.x, eye.y, eye.z);
    glUniform1f(prog.fog, 1.0f / (cam.sceneRadius() * 6.0f + 50.0f));
    glUniform3f(prog.fogColor, (bgTop.x + bgBottom.x) * 0.5f, (bgTop.y + bgBottom.y) * 0.5f, (bgTop.z + bgBottom.z) * 0.5f);

    // Relaxed smooth meshes can contain a few folded triangles, so draw them two-sided.
    if (!smooth_) glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    if (s.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    // Opaque pass
    std::vector<std::pair<float, const GpuChunk*>> transparent;
    const float cs = float(kChunkSize);
    for (const GpuChunk& c : chunks_) {
        if (c.origin.y > s.clipY) continue;
        if (!boxVisible(planes, c.origin - Vec3(1, 1, 1), c.origin + Vec3(cs + 2, cs + 2, cs + 2))) continue;
        if (c.opaque.indexCount) {
            glUniform3f(prog.origin, c.origin.x, c.origin.y, c.origin.z);
            glBindVertexArray(c.opaque.vao);
            glDrawElements(GL_TRIANGLES, c.opaque.indexCount, GL_UNSIGNED_INT, nullptr);
            drawnTriangles_ += size_t(c.opaque.indexCount / 3);
        }
        if (c.transparent.indexCount) {
            Vec3 d = c.origin + Vec3(cs * 0.5f, cs * 0.5f, cs * 0.5f) - eye;
            transparent.emplace_back(dot(d, d), &c);
        }
    }

    // Transparent pass, chunks sorted back to front.
    if (!transparent.empty()) {
        std::sort(transparent.begin(), transparent.end(), [](auto& a, auto& b) { return a.first > b.first; });
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glDepthMask(GL_FALSE);
        for (auto& t : transparent) {
            const GpuChunk& c = *t.second;
            glUniform3f(prog.origin, c.origin.x, c.origin.y, c.origin.z);
            glBindVertexArray(c.transparent.vao);
            glDrawElements(GL_TRIANGLES, c.transparent.indexCount, GL_UNSIGNED_INT, nullptr);
            drawnTriangles_ += size_t(c.transparent.indexCount / 3);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    if (s.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_CULL_FACE);

    // Grid and bounds
    if (s.grid || s.bounds) {
        glUseProgram(lineProg_);
        glUniformMatrix4fv(uLineViewProj_, 1, GL_FALSE, vp.m);
        glBindVertexArray(lineVao_);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        if (s.grid) {
            float g = s.darkBackground ? 1.0f : 0.0f;
            float gc[4] = {g, g, g, 0.12f};
            glUniform4fv(uLineColor_, 1, gc);
            glDrawArrays(GL_LINES, 0, gridVerts_);
        }
        if (s.bounds) {
            float bc[4] = {1.0f, 0.75f, 0.2f, 0.8f};
            glUniform4fv(uLineColor_, 1, bc);
            glDrawArrays(GL_LINES, gridVerts_, boundsVerts_);
        }
        glDisable(GL_BLEND);
    }
    glBindVertexArray(0);
}

}  // namespace vox
