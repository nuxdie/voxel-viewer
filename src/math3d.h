// Minimal vector / matrix math for the viewer.
#pragma once

#include <cmath>

namespace vox {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) { float l = length(a); return l > 0 ? a * (1.0f / l) : a; }

// Column-major 4x4 matrix (OpenGL convention).
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float& operator()(int row, int col) { return m[col * 4 + row]; }
    float operator()(int row, int col) const { return m[col * 4 + row]; }
    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += (*this)(i, k) * o(k, j);
                r(i, j) = s;
            }
        return r;
    }
};

inline Mat4 perspective(float fovyRad, float aspect, float zNear, float zFar) {
    Mat4 r;
    float f = 1.0f / std::tan(fovyRad * 0.5f);
    r.m[0] = f / aspect; r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar); r.m[11] = -1;
    r.m[14] = 2 * zFar * zNear / (zNear - zFar); r.m[15] = 0;
    return r;
}

inline Mat4 ortho(float l, float r, float b, float t, float n, float f) {
    Mat4 m;
    m.m[0] = 2 / (r - l); m.m[5] = 2 / (t - b); m.m[10] = -2 / (f - n);
    m.m[12] = -(r + l) / (r - l); m.m[13] = -(t + b) / (t - b); m.m[14] = -(f + n) / (f - n);
    return m;
}

inline Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = normalize(center - eye), s = normalize(cross(f, up)), u = cross(s, f);
    Mat4 r;
    r(0, 0) = s.x; r(0, 1) = s.y; r(0, 2) = s.z;
    r(1, 0) = u.x; r(1, 1) = u.y; r(1, 2) = u.z;
    r(2, 0) = -f.x; r(2, 1) = -f.y; r(2, 2) = -f.z;
    r(0, 3) = -dot(s, eye); r(1, 3) = -dot(u, eye); r(2, 3) = dot(f, eye);
    return r;
}

}  // namespace vox
