//
// Created by juaquin-remon on 9/6/26.
//

#include "common/Camera.h"

Camera::Camera(glm::vec3 eye,
           glm::vec3 target,
           float fov,
           float aspect)
        : position(eye), target(target), up({0.0f, 1.0f, 0.0f}),
          fov_deg(fov), aspect_ratio(aspect), near_z(0.1f), far_z(100.0f){}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, target, up);
}

glm::mat4 Camera::getProjectionMatrix() const {
    return glm::perspective(glm::radians(fov_deg), aspect_ratio, near_z, far_z);
}

void Camera::rotateOrbit(float deltaX, float deltaY, float sensitivity) {
    glm::vec3 offset = position - target;
    float radius = glm::length(offset);
    if (radius < 1e-4f) return;

    glm::vec3 forward = glm::normalize(-offset);
    glm::vec3 right = glm::normalize(glm::cross(forward, up));
    glm::quat pitch = glm::angleAxis(-deltaY * sensitivity, right);

    glm::vec3 candidateOffset = pitch * offset;

    float cosAngle = glm::dot(glm::normalize(candidateOffset), up);

    if (std::abs(cosAngle) < 0.998f) {
        offset = candidateOffset;
    }

    glm::quat yaw = glm::angleAxis(-deltaX * sensitivity, up);
    offset = yaw * offset;

    position = target + offset;
}