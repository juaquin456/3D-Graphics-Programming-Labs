#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace vrm {

class Camera {
public:
    Camera(glm::vec3 target = glm::vec3(0.0f, 0.5f, 0.0f),
           float distance = 2.5f,
           float yaw = 45.0f,
           float pitch = 20.0f,
           float fov = 45.0f);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspectRatio) const;
    glm::mat4 getInverseViewMatrix() const;
    glm::mat4 getInverseProjectionMatrix(float aspectRatio) const;

    glm::vec3 getPosition() const;
    glm::vec3 getTarget() const { return m_target; }
    glm::vec3 getForward() const;

    float getFov() const { return m_fov; }
    float getNearPlane() const { return m_nearPlane; }
    float getFarPlane() const { return m_farPlane; }
    float getDistance() const { return m_distance; }
    float getYaw() const { return m_yaw; }
    float getPitch() const { return m_pitch; }

    void setTarget(const glm::vec3& target) { m_target = target; }
    void setDistance(float distance);
    void setAngles(float yaw, float pitch);
    void setFov(float fov) { m_fov = glm::clamp(fov, 10.0f, 120.0f); }
    void setPlanes(float nearP, float farP) { m_nearPlane = nearP; m_farPlane = farP; }

    void processMouseMovement(float deltaX, float deltaY, bool isOrbit, bool isPan);
    void processMouseScroll(float yOffset);

private:
    glm::vec3 m_target;
    float m_distance;
    float m_yaw;    // in degrees
    float m_pitch;  // in degrees
    float m_fov;    // in degrees
    float m_nearPlane;
    float m_farPlane;

    float m_orbitSensitivity;
    float m_panSensitivity;
    float m_zoomSensitivity;
};

} // namespace vrm
