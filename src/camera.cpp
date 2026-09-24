#include "camera.h"

#include <algorithm>

namespace vox {

static constexpr float kDeg = 3.14159265358979f / 180.0f;

void Camera::frame(const Vec3& mn, const Vec3& mx) {
    target_ = sceneCenter_ = (mn + mx) * 0.5f;
    radius_ = std::max(1.0f, length(mx - mn) * 0.5f);
    distance_ = radius_ / std::sin(fovDeg * 0.5f * kDeg) * 1.05f;
    yaw_ = 45.0f;
    pitch_ = 30.0f;
    mode_ = Mode::Orbit;
}

Vec3 Camera::forward() const {
    float cy = std::cos(yaw_ * kDeg), sy = std::sin(yaw_ * kDeg);
    float cp = std::cos(pitch_ * kDeg), sp = std::sin(pitch_ * kDeg);
    // Looking from eye toward target; yaw rotates around Y, positive pitch looks down.
    return normalize(Vec3(-sy * cp, -sp, -cy * cp));
}

Vec3 Camera::eye() const { return mode_ == Mode::Fly ? flyPos_ : target_ - forward() * distance_; }

void Camera::orbit(float dx, float dy) {
    yaw_ -= dx * 0.3f;
    pitch_ = std::clamp(pitch_ + dy * 0.3f, -89.0f, 89.0f);
}

void Camera::look(float dx, float dy) {
    yaw_ -= dx * 0.15f;
    pitch_ = std::clamp(pitch_ + dy * 0.15f, -89.0f, 89.0f);
}

void Camera::pan(float dx, float dy, int viewportHeight) {
    Vec3 f = forward();
    Vec3 right = normalize(cross(f, Vec3(0, 1, 0)));
    Vec3 up = cross(right, f);
    float worldPerPixel = 2.0f * distance_ * std::tan(fovDeg * 0.5f * kDeg) / float(std::max(1, viewportHeight));
    Vec3 delta = right * (-dx * worldPerPixel) + up * (dy * worldPerPixel);
    if (mode_ == Mode::Fly) flyPos_ += delta;
    else target_ += delta;
}

void Camera::zoom(float steps) {
    if (mode_ == Mode::Fly) {
        flyPos_ += forward() * (steps * radius_ * 0.05f);
        return;
    }
    distance_ = std::clamp(distance_ * std::pow(0.88f, steps), 0.5f, radius_ * 50.0f);
}

void Camera::move(const Vec3& d, float dt, bool fast) {
    Vec3 f = forward();
    Vec3 right = normalize(cross(f, Vec3(0, 1, 0)));
    float speed = std::max(4.0f, radius_ * 0.4f) * (fast ? 4.0f : 1.0f) * dt;
    flyPos_ += (right * d.x + Vec3(0, 1, 0) * d.y + f * d.z) * speed;
}

void Camera::setMode(Mode m) {
    if (m == mode_) return;
    if (m == Mode::Fly) {
        flyPos_ = eye();
    } else {
        // Re-target the orbit to a point in front of the current position.
        target_ = flyPos_ + forward() * distance_;
    }
    mode_ = m;
}

Mat4 Camera::view() const {
    Vec3 e = eye();
    return lookAt(e, e + forward(), Vec3(0, 1, 0));
}

Mat4 Camera::projection(float aspect) const {
    float farPlane = length(eye() - sceneCenter_) + radius_ * 2.0f + 100.0f;
    float nearPlane = std::max(0.05f, farPlane / 20000.0f);
    return perspective(fovDeg * kDeg, aspect, nearPlane, farPlane);
}

}  // namespace vox
