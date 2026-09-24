#pragma once

#include "math3d.h"

namespace vox {

// Orbit camera around a target, with an optional free-fly mode.
class Camera {
public:
    enum class Mode { Orbit, Fly };

    void frame(const Vec3& boundsMin, const Vec3& boundsMax);  // fit the model in view
    void orbit(float dxPixels, float dyPixels);
    void pan(float dxPixels, float dyPixels, int viewportHeight);
    void zoom(float wheelSteps);
    void look(float dxPixels, float dyPixels);          // fly mode mouse look
    void move(const Vec3& localDir, float dt, bool fast);  // fly mode: x=right, y=up, z=forward
    void setMode(Mode m);
    Mode mode() const { return mode_; }
    void setView(float yawDeg, float pitchDeg) { yaw_ = yawDeg; pitch_ = pitchDeg; }

    Vec3 eye() const;
    Vec3 forward() const;
    Mat4 view() const;
    Mat4 projection(float aspect) const;
    float sceneRadius() const { return radius_; }

    float fovDeg = 50.0f;

private:
    Mode mode_ = Mode::Orbit;
    Vec3 target_;
    Vec3 sceneCenter_;
    Vec3 flyPos_;
    float yaw_ = 45.0f, pitch_ = 30.0f;  // degrees
    float distance_ = 10.0f;
    float radius_ = 10.0f;
};

}  // namespace vox
