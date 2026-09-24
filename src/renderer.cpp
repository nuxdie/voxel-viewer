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

// ---- Watercolor ---------------------------------------------------------------------------

const char* kSplatVs = R"(#version 330 core
layout(location = 0) in uvec4 aPosSeed;   // chunk-local voxel x, y, z and a random seed
layout(location = 1) in vec3 aNormal;
layout(location = 2) in uint aFlags;      // bit 0 emissive, bit 1 transparent
layout(location = 3) in vec4 aColor;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uOrigin;
uniform float uClipY;
uniform vec3 uLightDir;   // view independent, world space
uniform float uSize;      // splat radius in voxels
out vec2 vUv;
out vec3 vColor;
flat out float vSeed;
const vec2 kCorners[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
void main() {
    vec3 c = vec3(aPosSeed.xyz) + 0.5 + uOrigin;
    if (c.y > uClipY) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
    float seed = float(aPosSeed.w) / 255.0;
    vSeed = seed;
    vec2 corner = kCorners[gl_VertexID];
    vUv = corner;
    // Every blob gets its own size and rotation so the strokes do not line up in a grid.
    float size = uSize * (0.85 + 0.35 * fract(seed * 7.31));
    float ang = seed * 6.2831853;
    mat2 rot = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));
    vec4 vp = uView * vec4(c, 1.0);
    vp.xy += rot * corner * size;
    gl_Position = uProj * vp;

    // Painterly shading: a few soft value steps, cool blue-violet shadows, warm light.
    vec3 base = aColor.rgb;
    if ((aFlags & 1u) != 0u) {
        vColor = base;
    } else {
        vec3 n = normalize(aNormal);
        float d = max(dot(n, uLightDir), 0.0);
        d = smoothstep(0.0, 1.0, d);
        float sky = 0.5 + 0.5 * n.y;
        vec3 shadow = base * vec3(0.48, 0.52, 0.74);
        vec3 lit = base * vec3(1.06, 1.03, 0.96);
        vColor = mix(shadow, lit, clamp(0.15 + 0.85 * d + 0.15 * sky, 0.0, 1.0));
        // Highlights: leave the paper nearly bare where the light hits squarely.
        vColor = mix(vColor, vec3(1.0), 0.35 * pow(d, 6.0));
    }
    if ((aFlags & 2u) != 0u) vColor = mix(vec3(1.0), vColor, 0.35 + 0.5 * aColor.a);  // thin, watery washes
}
)";

const char* kSplatFs = R"(#version 330 core
in vec2 vUv;
in vec3 vColor;
flat in float vSeed;
out vec4 fragColor;
float hash(float n) { return fract(sin(n) * 43758.5453); }
void main() {
    float r = length(vUv);
    float a = atan(vUv.y, vUv.x);
    float s = vSeed * 97.0;
    // Irregular, wobbly blob outline.
    float edge = 0.80 + 0.09 * sin(3.0 * a + s) + 0.06 * sin(5.0 * a + 2.3 * s) + 0.04 * sin(11.0 * a + 5.1 * s);
    if (r > edge) discard;
    // Each dab carries a slightly different amount of pigment, pooling toward its rim.
    float amount = 0.95 + 0.1 * hash(s);
    float rim = smoothstep(edge - 0.25, edge, r);
    vec3 c = 1.0 - (1.0 - vColor) * amount * (1.0 + 0.06 * rim);
    fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

// Turns the splat image into a watercolor painting on paper.
const char* kPaintFs = R"(#version 330 core
uniform sampler2D uColor;
uniform sampler2D uDepth;
uniform vec2 uTexel;
out vec4 fragColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, amp = 0.5;
    for (int i = 0; i < 4; ++i) { v += amp * vnoise(p); p = p * 2.03 + 11.7; amp *= 0.5; }
    return v;
}
void main() {
    vec2 px = gl_FragCoord.xy;
    vec2 uv = px * uTexel;
    // Hand-drawn wobble of every edge.
    vec2 wob = vec2(fbm(px / 70.0), fbm(px / 70.0 + 23.1)) - 0.5;
    vec2 suv = uv + wob * 9.0 * uTexel;
    vec3 c = texture(uColor, suv).rgb;

    // Wet-in-wet bleeding: a noisy disk blur.
    vec3 blur = vec3(0.0);
    float angle = hash(px) * 6.2831853;
    for (int i = 0; i < 16; ++i) {
        float t = (float(i) + 0.5) / 16.0;
        float rad = 6.0 * sqrt(t);
        float an = angle + float(i) * 2.39996;
        blur += texture(uColor, suv + vec2(cos(an), sin(an)) * rad * uTexel).rgb;
    }
    blur /= 16.0;

    // Pigment is a transmittance: 1 lets the paper through, 0 is full pigment. Work with
    // density (-log transmittance) so darkening deepens the pigment's own hue instead of greying.
    vec3 pig = clamp(mix(c, blur, 0.45), 0.02, 1.0);
    // Edge darkening: pigment pools where washes meet each other or dry paper.
    float edge = smoothstep(0.02, 0.25, length(c - blur));
    float density = 1.0 + 0.9 * edge;
    // Uneven washes and a little granulation into the paper grain.
    float wash = fbm(px / 220.0 + 5.0);
    density *= 0.8 + 0.4 * wash;
    float gran = fbm(px / 6.0 + 1.7);
    density *= 0.92 + 0.16 * gran;
    pig = pow(pig, vec3(density));
    // Watercolor never fully covers the paper.
    pig = mix(vec3(1.0), pig, 0.92);

    // Cold-press paper: warm white with fibers and tooth.
    float fiber = fbm(vec2(px.x / 1.5, px.y / 9.0)) * 0.5 + fbm(px / 2.0) * 0.5;
    vec3 paper = vec3(0.972, 0.955, 0.915) * (0.95 + 0.07 * fiber);
    fragColor = vec4(paper * pig, 1.0);
}
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
    splatProg_ = link(kSplatVs, kSplatFs);
    paintProg_ = link(kBgVs, kPaintFs);
    if (!splatProg_ || !paintProg_) return false;
    uSplatView_ = glGetUniformLocation(splatProg_, "uView");
    uSplatProj_ = glGetUniformLocation(splatProg_, "uProj");
    uSplatOrigin_ = glGetUniformLocation(splatProg_, "uOrigin");
    uSplatClipY_ = glGetUniformLocation(splatProg_, "uClipY");
    uSplatLight_ = glGetUniformLocation(splatProg_, "uLightDir");
    uSplatSize_ = glGetUniformLocation(splatProg_, "uSize");
    uPaintColor_ = glGetUniformLocation(paintProg_, "uColor");
    uPaintDepth_ = glGetUniformLocation(paintProg_, "uDepth");
    uPaintTexel_ = glGetUniformLocation(paintProg_, "uTexel");

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
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (fboColor_) glDeleteTextures(1, &fboColor_);
    if (fboDepth_) glDeleteTextures(1, &fboDepth_);
    for (GLuint p : {blockyProg_.id, smoothProg_.id, splatProg_, paintProg_, lineProg_, bgProg_})
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
    mode_ = Mode::Blocky;

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
    mode_ = Mode::Smooth;
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

void Renderer::upload(const std::vector<SplatChunk>& chunks, IVec3 bmin, IVec3 bmax) {
    beginUpload(bmin, bmax);
    mode_ = Mode::Watercolor;
    chunks_.reserve(chunks.size());
    for (const auto& sc : chunks) {
        GpuChunk c;
        c.origin = Vec3(float(sc.chunk.x * kChunkSize), float(sc.chunk.y * kChunkSize), float(sc.chunk.z * kChunkSize));
        c.opaque = makeMesh(sc.splats);
        chunks_.push_back(c);
    }
    buildGrid();
}

Renderer::GpuMesh Renderer::makeMesh(const std::vector<Splat>& splats) {
    GpuMesh m;
    if (splats.empty()) return m;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(splats.size() * sizeof(Splat)), splats.data(), GL_STATIC_DRAW);
    const GLsizei stride = sizeof(Splat);
    glEnableVertexAttribArray(0);
    glVertexAttribIPointer(0, 4, GL_UNSIGNED_BYTE, stride, reinterpret_cast<void*>(offsetof(Splat, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(Splat, nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, stride, reinterpret_cast<void*>(offsetof(Splat, flags)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(Splat, r)));
    for (GLuint a = 0; a < 4; ++a) glVertexAttribDivisor(a, 1);  // one splat per instance
    glBindVertexArray(0);
    m.indexCount = GLsizei(splats.size());
    gpuBytes_ += splats.size() * sizeof(Splat);
    totalTriangles_ += splats.size() * 2;
    return m;
}

bool Renderer::ensureFbo(int w, int h) {
    if (fbo_ && w == fboW_ && h == fboH_) return true;
    if (!fbo_) {
        glGenFramebuffers(1, &fbo_);
        glGenTextures(1, &fboColor_);
        glGenTextures(1, &fboDepth_);
    }
    glBindTexture(GL_TEXTURE_2D, fboColor_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    for (GLuint t : {fboColor_, fboDepth_}) {
        glBindTexture(GL_TEXTURE_2D, t);
        if (t == fboDepth_)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboColor_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fboDepth_, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        std::fprintf(stderr, "watercolor: framebuffer incomplete\n");
        return false;
    }
    fboW_ = w;
    fboH_ = h;
    return true;
}

void Renderer::renderWatercolor(const Camera& cam, int width, int height, const RenderSettings& s) {
    drawnTriangles_ = 0;
    if (!ensureFbo(width, height)) return;
    float aspect = float(width) / float(std::max(1, height));
    Mat4 view = cam.view(), proj = cam.projection(aspect);
    auto planes = frustumPlanes(proj * view);

    // 1. Pigment splats into an offscreen buffer (white = bare paper).
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width, height);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    if (hasModel_) {
        glUseProgram(splatProg_);
        glUniformMatrix4fv(uSplatView_, 1, GL_FALSE, view.m);
        glUniformMatrix4fv(uSplatProj_, 1, GL_FALSE, proj.m);
        Vec3 light = normalize(Vec3(0.45f, 0.85f, 0.3f));
        glUniform3f(uSplatLight_, light.x, light.y, light.z);
        glUniform1f(uSplatClipY_, s.clipY);
        glUniform1f(uSplatSize_, 0.95f);
        const float cs = float(kChunkSize);
        for (const GpuChunk& c : chunks_) {
            if (c.origin.y > s.clipY || !c.opaque.indexCount) continue;
            if (!boxVisible(planes, c.origin - Vec3(2, 2, 2), c.origin + Vec3(cs + 2, cs + 2, cs + 2))) continue;
            glUniform3f(uSplatOrigin_, c.origin.x, c.origin.y, c.origin.z);
            glBindVertexArray(c.opaque.vao);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, c.opaque.indexCount);
            drawnTriangles_ += size_t(c.opaque.indexCount) * 2;
        }
    }

    // 2. Paint: wobble, bleeding, edge darkening, granulation and paper.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(paintProg_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fboColor_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, fboDepth_);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(uPaintColor_, 0);
    glUniform1i(uPaintDepth_, 1);
    glUniform2f(uPaintTexel_, 1.0f / float(width), 1.0f / float(height));
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::render(const Camera& cam, int width, int height, const RenderSettings& s) {
    if (mode_ == Mode::Watercolor) {
        renderWatercolor(cam, width, height, s);
        return;
    }
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

    const VoxelProgram& prog = mode_ == Mode::Smooth ? smoothProg_ : blockyProg_;
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
    if (mode_ == Mode::Blocky) glEnable(GL_CULL_FACE);
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
