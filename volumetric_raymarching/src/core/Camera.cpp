#include "Camera.h"
#include <cmath>

namespace vrm {

Camera::Camera(glm::vec3 target, float distance, float yaw, float pitch, float fov)
    : m_target(target),
      m_distance(distance),
      m_yaw(yaw),
      m_pitch(pitch),
      m_fov(fov),
      m_nearPlane(0.05f),
      m_farPlane(50.0f),
      m_orbitSensitivity(0.25f),
      m_panSensitivity(0.003f),
      m_zoomSensitivity(0.25f)
{
    m_pitch = glm::clamp(m_pitch, -89.0f, 89.0f);
}

glm::vec3 Camera::getPosition() const {
    float yawRad = glm::radians(m_yaw);
    float pitchRad = glm::radians(m_pitch);

    float x = m_distance * std::cos(pitchRad) * std::sin(yawRad);
    float y = m_distance * std::sin(pitchRad);
    float z = m_distance * std::cos(pitchRad) * std::cos(yawRad);

    return m_target + glm::vec3(x, y, z);
}

glm::vec3 Camera::getForward() const {
    return glm::normalize(m_target - getPosition());
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(getPosition(), m_target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const {
    return glm::perspective(glm::radians(m_fov), aspectRatio, m_nearPlane, m_farPlane);
}

glm::mat4 Camera::getInverseViewMatrix() const {
    return glm::inverse(getViewMatrix());
}

glm::mat4 Camera::getInverseProjectionMatrix(float aspectRatio) const {
    return glm::inverse(getProjectionMatrix(aspectRatio));
}

void Camera::setDistance(float distance) {
    m_distance = glm::clamp(distance, 0.2f, 30.0f);
}

void Camera::setAngles(float yaw, float pitch) {
    m_yaw = yaw;
    m_pitch = glm::clamp(pitch, -89.0f, 89.0f);
}

void Camera::processMouseMovement(float deltaX, float deltaY, bool isOrbit, bool isPan) {
    if (isOrbit) {
        m_yaw   += deltaX * m_orbitSensitivity;
        m_pitch -= deltaY * m_orbitSensitivity;
        m_pitch = glm::clamp(m_pitch, -89.0f, 89.0f);
    } else if (isPan) {
        glm::vec3 pos = getPosition();
        glm::vec3 fwd = glm::normalize(m_target - pos);
        glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 up = glm::normalize(glm::cross(right, fwd));

        float panSpeed = m_panSensitivity * m_distance;
        m_target -= right * (deltaX * panSpeed);
        m_target += up    * (deltaY * panSpeed);
    }
}

void Camera::processMouseScroll(float yOffset) {
    m_distance -= yOffset * m_zoomSensitivity;
    m_distance = glm::clamp(m_distance, 0.2f, 30.0f);
}

} // namespace vrm
