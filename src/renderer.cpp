#include "renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>

namespace vox {
namespace {

// Shared lighting, inserted into every shading program so all render modes are lit the same way:
// a warm sun with shadow-mapped soft shadows, cool sky light and ground bounce (both scaled by
// ambient occlusion), aerial perspective toward the horizon, and one tone curve.
const char* kLightingGlsl = R"(
uniform vec3 uLightDir;      // toward the sun (or moon), world space
uniform vec3 uSunColor;      // linear
uniform vec3 uSkyColor;      // ambient sky light, linear
uniform vec3 uBounceColor;   // light bounced off the ground, linear
uniform vec3 uSkyTop;        // sky gradient for reflections, linear
uniform mat4 uLightVP;
uniform sampler2D uShadowMap;
uniform vec2 uShadowTexel;
uniform float uShadowsOn;
uniform vec3 uEye;
uniform float uFogDist;
uniform vec3 uHorizon;       // linear
uniform float uAoStrength;
float shadowAt(vec3 p) {
    if (uShadowsOn < 0.5) return 1.0;
    vec3 q = (uLightVP * vec4(p, 1.0)).xyz * 0.5 + 0.5;
    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0 || q.z > 1.0) return 1.0;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            float d = textureLod(uShadowMap, q.xy + vec2(float(x), float(y)) * uShadowTexel * 1.5, 0.0).r;
            lit += q.z - 0.0015 > d ? 0.0 : 1.0;
        }
    return lit / 9.0;
}
// Block light level (0-15 per channel) to linear intensity; like Minecraft, each level is ~20% dimmer.
vec3 blockLightColor(vec3 level) {
    return 1.6 * pow(vec3(0.8), 15.0 - level) * step(vec3(0.05), level);
}
// What a mirror sees in direction d: the sky above the horizon, the ground below it.
vec3 skyRadiance(vec3 d) {
    if (d.y >= 0.0) return mix(uHorizon, uSkyTop, pow(clamp(d.y, 0.0, 1.0), 0.6));
    return mix(uHorizon, uBounceColor * 2.5, clamp(-d.y * 4.0, 0.0, 1.0));
}
// base: linear albedo, n: unit normal, p: point just off the surface, ao: 1 = open, lower = enclosed,
// block: block light (linear), rough: 0 = mirror .. 1 = matte, metal: 0 or 1.
// Returns linear radiance; fresnel receives how reflective the surface is from this angle.
vec3 shade(vec3 base, vec3 n, vec3 p, float ao, bool emissive, vec3 block, float rough, float metal,
           out float fresnel) {
    fresnel = 0.0;
    if (emissive) return base * 1.3;
    ao = mix(1.0, ao, uAoStrength);
    float ndl = max(dot(n, uLightDir), 0.0);
    float sh = ndl > 0.0 ? shadowAt(p) : 0.0;
    vec3 ambient = (uSkyColor * (0.55 + 0.45 * n.y) + uBounceColor * (0.5 - 0.5 * n.y)) * ao;
    vec3 light = uSunColor * ndl * sh * mix(1.0, ao, 0.35) + ambient + block * mix(1.0, ao, 0.5);
    vec3 diffuse = base * (1.0 - metal) * light;

    // Reflections: the sky (blurred toward ambient with roughness) plus a sun highlight, weighted by
    // Schlick Fresnel. Only glossy surfaces get the strong grazing-angle boost.
    vec3 v = normalize(uEye - p);
    float ndv = max(dot(n, v), 0.0);
    float gloss = (1.0 - rough) * (1.0 - rough);
    vec3 f0 = mix(vec3(0.04), base, metal);
    vec3 F = f0 + (1.0 - f0) * pow(1.0 - ndv, 5.0) * gloss;
    fresnel = max(F.r, max(F.g, F.b));
    vec3 env = mix(skyRadiance(reflect(-v, n)), uSkyColor * 0.8 + block * 0.3, rough) * ao;
    vec3 h = normalize(uLightDir + v);
    float r4 = max(rough * rough * rough * rough, 1e-4);
    float shininess = min(2.0 / r4 - 2.0, 4096.0);
    float spec = pow(max(dot(n, h), 0.0), shininess) * (shininess + 8.0) / 25.13;
    vec3 specular = env * F + uSunColor * sh * ndl * spec * F;
    return diffuse * (1.0 - fresnel) + specular;
}
// Aerial perspective, soft shoulder tone curve, display gamma.
vec3 finishColor(vec3 col, vec3 p) {
    float fog = 1.0 - exp(-pow(length(p - uEye) / uFogDist, 1.6));
    col = mix(col, uHorizon, clamp(fog, 0.0, 0.75));
    col = col / (1.0 + 0.15 * col);
    return pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2));
}
)";

// Inserts the shared lighting code after a shader's #version line.
std::string withLighting(const char* src) {
    std::string s = src;
    size_t eol = s.find('\n');
    return s.substr(0, eol + 1) + kLightingGlsl + s.substr(eol + 1);
}

const char* kVoxelVs = R"(#version 330 core
layout(location = 0) in uvec4 aPos;   // x, y, z (chunk local), normal | ao << 3 | emissive << 5
layout(location = 1) in vec4 aColor;
layout(location = 2) in uvec4 aLightSurface;  // block light rgb (level * 17), surface finish
uniform mat4 uViewProj;
uniform vec3 uOrigin;
out vec4 vColor;
out vec3 vWorld;
out vec3 vNormal;
out float vAo;
out vec3 vBlock;
flat out vec2 vSurface;   // roughness, metal
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
    vBlock = blockLightColor(vec3(aLightSurface.rgb) / 17.0);
    vSurface = vec2(float(aLightSurface.a & 15u) / 15.0, float((aLightSurface.a >> 4) & 1u));
    gl_Position = uViewProj * vec4(p, 1.0);
}
)";

const char* kSmoothVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;      // chunk-local fixed point: p / 1024 - 2
layout(location = 1) in vec3 aNormal;   // normalized snorm8
layout(location = 2) in uint aAoEmissive;
layout(location = 3) in vec4 aColor;
layout(location = 4) in uvec4 aLightSurface;  // block light rgb (level * 17), surface finish
uniform mat4 uViewProj;
uniform vec3 uOrigin;
out vec4 vColor;
out vec3 vWorld;
out vec3 vNormal;
out float vAo;
out vec3 vBlock;
flat out vec2 vSurface;   // roughness, metal
flat out int vEmissive;
void main() {
    vBlock = blockLightColor(vec3(aLightSurface.rgb) / 17.0);
    vSurface = vec2(float(aLightSurface.a & 15u) / 15.0, float((aLightSurface.a >> 4) & 1u));
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
in vec3 vBlock;
flat in vec2 vSurface;
flat in int vEmissive;
uniform float uClipY;
out vec4 fragColor;
void main() {
    if (vWorld.y > uClipY) discard;
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 base = pow(vColor.rgb, vec3(2.2));
    float fresnel;
    vec3 col = shade(base, n, vWorld + n * 0.6, vAo, vEmissive != 0, vBlock, vSurface.x, vSurface.y, fresnel);
    // Glass and water get more opaque where they reflect more (grazing angles).
    fragColor = vec4(finishColor(col, vWorld), mix(vColor.a, 1.0, fresnel));
}
)";

// Shadow pass for meshes: depth only, honoring the cut-away slice.
const char* kShadowMeshFs = R"(#version 330 core
in vec3 vWorld;
uniform float uClipY;
void main() { if (vWorld.y > uClipY) discard; }
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
layout(location = 4) in float aLight;     // ambient occlusion
layout(location = 5) in uvec4 aLightSurface;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uOrigin;
uniform float uClipY;
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

    // Same lighting as every other mode; the paper pass adds the watercolor look.
    vec3 n = dot(aNormal, aNormal) > 1e-4 ? normalize(aNormal) : vec3(0.0, 1.0, 0.0);
    vec3 base = pow(aColor.rgb, vec3(2.2));
    float fresnel;
    vec3 col = shade(base, n, c + n * 0.3, aLight, (aFlags & 1u) != 0u,
                     blockLightColor(vec3(aLightSurface.rgb) / 17.0), float(aLightSurface.a & 15u) / 15.0,
                     float((aLightSurface.a >> 4) & 1u), fresnel);
    vColor = finishColor(col, c);
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

// ---- Painted style ------------------------------------------------------------------------

const char* kPaintedVs = R"(#version 330 core
layout(location = 0) in uvec4 aPosSeed;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in uint aFlags;      // bit 0 emissive, bit 1 transparent
layout(location = 3) in vec4 aColor;
layout(location = 4) in float aLight;     // ambient occlusion
layout(location = 5) in uvec4 aLightSurface;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uOrigin;
uniform float uClipY;
uniform float uSize;
out vec2 vUv;
out vec3 vColor;
flat out float vSeed;
const vec2 kCorners[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
float hash(float n) { return fract(sin(n) * 43758.5453); }
void main() {
    vec3 c = vec3(aPosSeed.xyz) + 0.5 + uOrigin;
    if (c.y > uClipY) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
    float seed = float(aPosSeed.w) / 255.0 + float(aPosSeed.x + aPosSeed.y * 7u + aPosSeed.z * 13u) * 0.0137;
    vSeed = seed;
    float h1 = hash(seed * 91.7), h2 = hash(seed * 37.3 + 1.1), h3 = hash(seed * 13.9 + 2.2);
    // Loose, hand-placed dabs: jitter the center so silhouettes break up like real brushwork.
    c += (vec3(hash(seed * 17.1), hash(seed * 29.3), hash(seed * 41.9)) - 0.5) * 0.45;

    // A short, slightly elongated brush dab with its own size and direction.
    vec2 corner = kCorners[gl_VertexID];
    vUv = corner;
    float elong = 1.0 + 0.45 * h1;
    float size = uSize * (0.8 + 0.4 * h2);
    float ang = h3 * 6.2831853;
    mat2 rot = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));
    vec4 vp = uView * vec4(c, 1.0);
    vp.xy += rot * (corner * vec2(size * elong, size / sqrt(elong)));
    gl_Position = uProj * vp;

    // Every dab gets a slightly different value and temperature.
    vec3 base = pow(aColor.rgb, vec3(2.2));
    base *= 0.86 + 0.28 * h2;
    base *= mix(vec3(1.05, 1.0, 0.93), vec3(0.94, 1.0, 1.07), h1);

    vec3 n = dot(aNormal, aNormal) > 1e-4 ? normalize(aNormal) : vec3(0.0, 1.0, 0.0);
    float fresnel;
    vec3 col = shade(base, n, c + n * 0.3, aLight, (aFlags & 1u) != 0u,
                     blockLightColor(vec3(aLightSurface.rgb) / 17.0), float(aLightSurface.a & 15u) / 15.0,
                     float((aLightSurface.a >> 4) & 1u), fresnel);
    vColor = finishColor(col, c);
}
)";

const char* kPaintedFs = R"(#version 330 core
in vec2 vUv;
in vec3 vColor;
flat in float vSeed;
out vec4 fragColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
void main() {
    float r = length(vUv);
    float a = atan(vUv.y, vUv.x);
    float s = vSeed * 97.0;
    float edge = 0.86 + 0.07 * sin(3.0 * a + s) + 0.05 * sin(7.0 * a + 2.3 * s);
    if (r > edge) discard;
    // Bristle streaks run along the stroke.
    float streak = vnoise(vec2(vUv.x * 1.3 + s, vUv.y * 7.0 + s * 0.37));
    vec3 c = vColor * (0.9 + 0.2 * streak);
    c *= 1.0 - 0.07 * smoothstep(0.45, 1.0, r / edge);
    fragColor = vec4(c, 1.0);
}
)";

// Shadow map: splats seen from the sun, depth only.
const char* kShadowVs = R"(#version 330 core
layout(location = 0) in uvec4 aPosSeed;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uOrigin;
uniform float uClipY;
uniform float uSize;
out vec2 vUv;
const vec2 kCorners[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
void main() {
    vec3 c = vec3(aPosSeed.xyz) + 0.5 + uOrigin;
    if (c.y > uClipY) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
    vUv = kCorners[gl_VertexID];
    vec4 vp = uView * vec4(c, 1.0);
    vp.xy += vUv * uSize;
    gl_Position = uProj * vp;
}
)";

const char* kShadowFs = R"(#version 330 core
in vec2 vUv;
void main() { if (dot(vUv, vUv) > 0.8) discard; }
)";

// Kuwahara filter: merges dabs into painterly patches of color while keeping edges crisp.
const char* kKuwaharaFs = R"(#version 330 core
uniform sampler2D uColor;
uniform vec2 uTexel;
out vec4 fragColor;
void main() {
    vec2 uv = gl_FragCoord.xy * uTexel;
    const int R = 3;
    vec3 m0 = vec3(0), m1 = vec3(0), m2 = vec3(0), m3 = vec3(0);
    vec3 s0 = vec3(0), s1 = vec3(0), s2 = vec3(0), s3 = vec3(0);
    for (int j = -R; j <= R; ++j)
        for (int i = -R; i <= R; ++i) {
            vec3 c = texture(uColor, uv + vec2(i, j) * uTexel).rgb;
            vec3 cc = c * c;
            if (i <= 0 && j <= 0) { m0 += c; s0 += cc; }
            if (i >= 0 && j <= 0) { m1 += c; s1 += cc; }
            if (i <= 0 && j >= 0) { m2 += c; s2 += cc; }
            if (i >= 0 && j >= 0) { m3 += c; s3 += cc; }
        }
    float n = float((R + 1) * (R + 1));
    m0 /= n; m1 /= n; m2 /= n; m3 /= n;
    vec3 v0 = s0 / n - m0 * m0, v1 = s1 / n - m1 * m1, v2 = s2 / n - m2 * m2, v3 = s3 / n - m3 * m3;
    float d0 = v0.r + v0.g + v0.b, d1 = v1.r + v1.g + v1.b, d2 = v2.r + v2.g + v2.b, d3 = v3.r + v3.g + v3.b;
    vec3 col = m0;
    float best = d0;
    if (d1 < best) { best = d1; col = m1; }
    if (d2 < best) { best = d2; col = m2; }
    if (d3 < best) { best = d3; col = m3; }
    // Richer color, gentle contrast and a soft vignette.
    float l = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(l), col, 1.18);
    col = mix(col, col * col * (3.0 - 2.0 * col), 0.25);
    vec2 d = uv - 0.5;
    col *= 1.0 - 0.28 * pow(length(d) * 1.25, 2.5);
    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
)";

GLuint compile(GLenum type, const char* src) {
#ifdef VV_GLES
    // Shaders are written as GLSL 3.30; OpenGL ES 3.0 needs its own header and precisions.
    std::string es = src;
    const std::string desktop = "#version 330 core\n";
    if (es.compare(0, desktop.size(), desktop) == 0)
        es = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n" +
             es.substr(desktop.size());
    src = es.c_str();
#endif
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

void Renderer::LightLocs::init(GLuint prog) {
    lightDir = glGetUniformLocation(prog, "uLightDir");
    lightVP = glGetUniformLocation(prog, "uLightVP");
    shadowMap = glGetUniformLocation(prog, "uShadowMap");
    shadowTexel = glGetUniformLocation(prog, "uShadowTexel");
    shadowsOn = glGetUniformLocation(prog, "uShadowsOn");
    eye = glGetUniformLocation(prog, "uEye");
    fogDist = glGetUniformLocation(prog, "uFogDist");
    horizon = glGetUniformLocation(prog, "uHorizon");
    ao = glGetUniformLocation(prog, "uAoStrength");
    sunColor = glGetUniformLocation(prog, "uSunColor");
    skyColor = glGetUniformLocation(prog, "uSkyColor");
    bounceColor = glGetUniformLocation(prog, "uBounceColor");
    skyTop = glGetUniformLocation(prog, "uSkyTop");
}

bool Renderer::VoxelProgram::init(const char* vs, const char* fs) {
    id = link(withLighting(vs).c_str(), withLighting(fs).c_str());
    if (!id) return false;
    viewProj = glGetUniformLocation(id, "uViewProj");
    origin = glGetUniformLocation(id, "uOrigin");
    clipY = glGetUniformLocation(id, "uClipY");
    light.init(id);
    return true;
}

bool Renderer::ShadowMeshProgram::init(const char* vs, const char* fs) {
    id = link(withLighting(vs).c_str(), fs);  // the mesh vertex shaders use the shared helpers
    if (!id) return false;
    viewProj = glGetUniformLocation(id, "uViewProj");
    origin = glGetUniformLocation(id, "uOrigin");
    clipY = glGetUniformLocation(id, "uClipY");
    return true;
}

bool Renderer::init() {
    splatProg_ = link(withLighting(kSplatVs).c_str(), kSplatFs);
    paintProg_ = link(kBgVs, kPaintFs);
    if (!splatProg_ || !paintProg_) return false;
    uSplatView_ = glGetUniformLocation(splatProg_, "uView");
    uSplatProj_ = glGetUniformLocation(splatProg_, "uProj");
    uSplatOrigin_ = glGetUniformLocation(splatProg_, "uOrigin");
    uSplatClipY_ = glGetUniformLocation(splatProg_, "uClipY");
    splatLight_.init(splatProg_);
    uSplatSize_ = glGetUniformLocation(splatProg_, "uSize");
    uPaintColor_ = glGetUniformLocation(paintProg_, "uColor");
    uPaintDepth_ = glGetUniformLocation(paintProg_, "uDepth");
    uPaintTexel_ = glGetUniformLocation(paintProg_, "uTexel");

    paintedProg_ = link(withLighting(kPaintedVs).c_str(), kPaintedFs);
    kuwaharaProg_ = link(kBgVs, kKuwaharaFs);
    shadowProg_ = link(kShadowVs, kShadowFs);
    if (!paintedProg_ || !kuwaharaProg_ || !shadowProg_) return false;
    {
        auto u = [&](const char* n) { return glGetUniformLocation(paintedProg_, n); };
        pl_.view = u("uView"); pl_.proj = u("uProj"); pl_.origin = u("uOrigin"); pl_.clipY = u("uClipY");
        pl_.size = u("uSize");
        pl_.light.init(paintedProg_);
    }
    uKuwColor_ = glGetUniformLocation(kuwaharaProg_, "uColor");
    uKuwTexel_ = glGetUniformLocation(kuwaharaProg_, "uTexel");
    uShView_ = glGetUniformLocation(shadowProg_, "uView");
    uShProj_ = glGetUniformLocation(shadowProg_, "uProj");
    uShOrigin_ = glGetUniformLocation(shadowProg_, "uOrigin");
    uShClipY_ = glGetUniformLocation(shadowProg_, "uClipY");
    uShSize_ = glGetUniformLocation(shadowProg_, "uSize");

    lineProg_ = link(kLineVs, kLineFs);
    bgProg_ = link(kBgVs, kBgFs);
    if (!blockyProg_.init(kVoxelVs, kVoxelFs) || !smoothProg_.init(kSmoothVs, kVoxelFs) || !lineProg_ || !bgProg_)
        return false;
    if (!shadowBlocky_.init(kVoxelVs, kShadowMeshFs) || !shadowSmooth_.init(kSmoothVs, kShadowMeshFs)) return false;

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
    if (shadowFbo_) glDeleteFramebuffers(1, &shadowFbo_);
    if (shadowTex_) glDeleteTextures(1, &shadowTex_);
    if (fboColor_) glDeleteTextures(1, &fboColor_);
    if (fboDepth_) glDeleteTextures(1, &fboDepth_);
    for (GLuint p : {blockyProg_.id, smoothProg_.id, splatProg_, paintProg_, paintedProg_, kuwaharaProg_, shadowProg_,
                     shadowBlocky_.id, shadowSmooth_.id, lineProg_, bgProg_})
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
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, sizeof(PackedVertex), reinterpret_cast<void*>(offsetof(PackedVertex, lr)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBindVertexArray(0);
    m.indexCount = GLsizei(verts.size() / 4 * 6);
    gpuBytes_ += verts.size() * sizeof(PackedVertex);
    totalTriangles_ += verts.size() / 2;
    return m;
}

void Renderer::beginUpload(IVec3 bmin, IVec3 bmax) {
    freeChunks();
    shadowDirty_ = true;
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
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, stride, reinterpret_cast<void*>(offsetof(SmoothVertex, lr)));
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
    shadowDirty_ = true;
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
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(Splat, light)));
    glEnableVertexAttribArray(5);
    glVertexAttribIPointer(5, 4, GL_UNSIGNED_BYTE, stride, reinterpret_cast<void*>(offsetof(Splat, lr)));
    for (GLuint a = 0; a < 6; ++a) glVertexAttribDivisor(a, 1);  // one splat per instance
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
        // Depth textures are not filterable in OpenGL ES.
        GLint filter = t == fboDepth_ ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
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

void Renderer::renderShadowMap(float clipY) {
    if (!shadowFbo_) {
        glGenFramebuffers(1, &shadowFbo_);
        glGenTextures(1, &shadowTex_);
        glBindTexture(GL_TEXTURE_2D, shadowTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowSize, kShadowSize, 0, GL_DEPTH_COMPONENT,
                     GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
        GLenum none = GL_NONE;
        glDrawBuffers(1, &none);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::fprintf(stderr, "painted: shadow framebuffer incomplete\n");
    }
    // Orthographic sun camera fitted around the model.
    Vec3 mn(float(bmin_.x), float(bmin_.y), float(bmin_.z)), mx(float(bmax_.x + 1), float(bmax_.y + 1), float(bmax_.z + 1));
    Vec3 center = (mn + mx) * 0.5f;
    float r = length(mx - mn) * 0.5f + 2.0f;
    Vec3 light = normalize(Vec3(0.45f, 0.85f, 0.3f));
    Mat4 view = lookAt(center + light * (2.0f * r), center, Vec3(0, 1, 0));
    Mat4 proj = ortho(-r, r, -r, r, 0.5f * r, 3.5f * r);
    lightVP_ = proj * view;

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, kShadowSize, kShadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    if (mode_ == Mode::Watercolor) {
        // Splats cast shadows as sun-facing discs.
        glUseProgram(shadowProg_);
        glUniformMatrix4fv(uShView_, 1, GL_FALSE, view.m);
        glUniformMatrix4fv(uShProj_, 1, GL_FALSE, proj.m);
        glUniform1f(uShClipY_, clipY);
        glUniform1f(uShSize_, 0.75f);
        for (const GpuChunk& c : chunks_) {
            if (c.origin.y > clipY || !c.opaque.indexCount) continue;
            glUniform3f(uShOrigin_, c.origin.x, c.origin.y, c.origin.z);
            glBindVertexArray(c.opaque.vao);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, c.opaque.indexCount);
        }
    } else {
        // Meshes cast shadows with their real geometry. Transparent blocks do not block the sun.
        const ShadowMeshProgram& sp = mode_ == Mode::Smooth ? shadowSmooth_ : shadowBlocky_;
        glUseProgram(sp.id);
        glUniformMatrix4fv(sp.viewProj, 1, GL_FALSE, lightVP_.m);
        glUniform1f(sp.clipY, clipY);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 3.0f);
        for (const GpuChunk& c : chunks_) {
            if (c.origin.y > clipY || !c.opaque.indexCount) continue;
            glUniform3f(sp.origin, c.origin.x, c.origin.y, c.origin.z);
            glBindVertexArray(c.opaque.vao);
            glDrawElements(GL_TRIANGLES, c.opaque.indexCount, GL_UNSIGNED_INT, nullptr);
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    shadowDirty_ = false;
    shadowClip_ = clipY;
}

Renderer::Environment Renderer::environment(const RenderSettings& s) const {
    Environment e;
    auto lin = [](Vec3 c) { return Vec3(std::pow(c.x, 2.2f), std::pow(c.y, 2.2f), std::pow(c.z, 2.2f)); };
    if (s.night) {
        // Moonlight: dim and blue, so block light carries the scene.
        e.skyTop = Vec3(0.02f, 0.03f, 0.08f);
        e.skyBottom = Vec3(0.07f, 0.09f, 0.17f);
        e.sunColor = Vec3(0.13f, 0.16f, 0.28f);
        e.skyColor = Vec3(0.035f, 0.045f, 0.09f);
        e.bounceColor = Vec3(0.01f, 0.012f, 0.02f);
    } else {
        e.skyTop = s.darkBackground ? Vec3(0.20f, 0.22f, 0.27f) : Vec3(0.40f, 0.63f, 0.92f);
        e.skyBottom = s.darkBackground ? Vec3(0.07f, 0.08f, 0.10f) : Vec3(0.84f, 0.90f, 0.96f);
        e.sunColor = Vec3(1.05f, 0.945f, 0.8f);
        e.skyColor = Vec3(0.40f, 0.50f, 0.75f);
        e.bounceColor = Vec3(0.12f, 0.09f, 0.06f);
    }
    // Distance haze fades toward the color near the horizon.
    Vec3 h = s.darkBackground && !s.night ? (e.skyTop + e.skyBottom) * 0.5f : e.skyBottom;
    e.horizonLinear = lin(h);
    // Reflections always see a real sky, even in front of the dark studio backdrop.
    e.skyTopLinear = s.night ? lin(e.skyTop) : lin(Vec3(0.40f, 0.63f, 0.92f));
    return e;
}

void Renderer::applyLighting(const LightLocs& l, const Camera& cam, const RenderSettings& s, const Environment& env) {
    Vec3 light = normalize(Vec3(0.45f, 0.85f, 0.3f));
    glUniform3f(l.lightDir, light.x, light.y, light.z);
    glUniformMatrix4fv(l.lightVP, 1, GL_FALSE, lightVP_.m);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(l.shadowMap, 1);
    glUniform2f(l.shadowTexel, 1.0f / kShadowSize, 1.0f / kShadowSize);
    glUniform1f(l.shadowsOn, s.shadows && shadowTex_ ? 1.0f : 0.0f);
    Vec3 eye = cam.eye();
    glUniform3f(l.eye, eye.x, eye.y, eye.z);
    glUniform1f(l.fogDist, cam.sceneRadius() * 9.0f + 60.0f);
    glUniform3f(l.horizon, env.horizonLinear.x, env.horizonLinear.y, env.horizonLinear.z);
    glUniform1f(l.ao, s.aoStrength);
    glUniform3f(l.sunColor, env.sunColor.x, env.sunColor.y, env.sunColor.z);
    glUniform3f(l.skyColor, env.skyColor.x, env.skyColor.y, env.skyColor.z);
    glUniform3f(l.bounceColor, env.bounceColor.x, env.bounceColor.y, env.bounceColor.z);
    glUniform3f(l.skyTop, env.skyTopLinear.x, env.skyTopLinear.y, env.skyTopLinear.z);
}

void Renderer::drawSky(const Environment& env) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(bgProg_);
    glUniform3f(uBgTop_, env.skyTop.x, env.skyTop.y, env.skyTop.z);
    glUniform3f(uBgBottom_, env.skyBottom.x, env.skyBottom.y, env.skyBottom.z);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::renderWatercolor(const Camera& cam, int width, int height, const RenderSettings& s, const Environment& skyEnv) {
    drawnTriangles_ = 0;
    const bool painted = s.paintStyle == 0;
    if (!ensureFbo(width, height)) return;
    float aspect = float(width) / float(std::max(1, height));
    Mat4 view = cam.view(), proj = cam.projection(aspect);
    auto planes = frustumPlanes(proj * view);

    // Watercolor paints on white paper, so distance haze fades toward the paper instead of the sky.
    Environment env = skyEnv;
    if (!painted) env.horizonLinear = Vec3(1, 1, 1);

    // 1. Lit splats into an offscreen buffer (white = bare paper for watercolor).
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width, height);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (painted) drawSky(env);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    if (hasModel_) {
        GLint origin;
        if (painted) {
            glUseProgram(paintedProg_);
            glUniformMatrix4fv(pl_.view, 1, GL_FALSE, view.m);
            glUniformMatrix4fv(pl_.proj, 1, GL_FALSE, proj.m);
            glUniform1f(pl_.clipY, s.clipY);
            glUniform1f(pl_.size, 0.9f);
            applyLighting(pl_.light, cam, s, env);
            origin = pl_.origin;
        } else {
            glUseProgram(splatProg_);
            glUniformMatrix4fv(uSplatView_, 1, GL_FALSE, view.m);
            glUniformMatrix4fv(uSplatProj_, 1, GL_FALSE, proj.m);
            glUniform1f(uSplatClipY_, s.clipY);
            glUniform1f(uSplatSize_, 0.95f);
            applyLighting(splatLight_, cam, s, env);
            origin = uSplatOrigin_;
        }
        const float cs = float(kChunkSize);
        for (const GpuChunk& c : chunks_) {
            if (c.origin.y > s.clipY || !c.opaque.indexCount) continue;
            if (!boxVisible(planes, c.origin - Vec3(2, 2, 2), c.origin + Vec3(cs + 2, cs + 2, cs + 2))) continue;
            glUniform3f(origin, c.origin.x, c.origin.y, c.origin.z);
            glBindVertexArray(c.opaque.vao);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, c.opaque.indexCount);
            drawnTriangles_ += size_t(c.opaque.indexCount) * 2;
        }
    }

    // 2. Style pass: Kuwahara for painted, wobble/bleeding/edge darkening/paper for watercolor.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fboColor_);
    if (painted) {
        glUseProgram(kuwaharaProg_);
        glUniform1i(uKuwColor_, 0);
        glUniform2f(uKuwTexel_, 1.0f / float(width), 1.0f / float(height));
    } else {
        glUseProgram(paintProg_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, fboDepth_);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(uPaintColor_, 0);
        glUniform1i(uPaintDepth_, 1);
        glUniform2f(uPaintTexel_, 1.0f / float(width), 1.0f / float(height));
    }
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::render(const Camera& cam, int width, int height, const RenderSettings& s) {
    // One sun, one shadow map, one sky for every mode.
    if (hasModel_ && s.shadows && (shadowDirty_ || shadowClip_ != s.clipY)) renderShadowMap(s.clipY);
    const Environment env = environment(s);
    if (mode_ == Mode::Watercolor) {
        renderWatercolor(cam, width, height, s, env);
        return;
    }
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawSky(env);
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
    glUniform1f(prog.clipY, s.clipY);
    applyLighting(prog.light, cam, s, env);

    // Relaxed smooth meshes can contain a few folded triangles, so draw them two-sided.
    if (mode_ == Mode::Blocky) glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
#ifndef VV_GLES
    if (s.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif

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
#ifndef VV_GLES
    if (s.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
    glDisable(GL_CULL_FACE);

    // Grid and bounds
    if (s.grid || s.bounds) {
        glUseProgram(lineProg_);
        glUniformMatrix4fv(uLineViewProj_, 1, GL_FALSE, vp.m);
        glBindVertexArray(lineVao_);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        if (s.grid) {
            float gc[4] = {1.0f, 1.0f, 1.0f, s.night ? 0.06f : s.darkBackground ? 0.12f : 0.3f};
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
