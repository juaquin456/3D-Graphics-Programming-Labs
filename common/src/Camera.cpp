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
    float cameraDistance = glm::length(position - target);
    glm::mat4 rotationMatrix = glm::mat4_cast(cameraOrientation);
    glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -cameraDistance));

    return translationMatrix * rotationMatrix;
}

glm::mat4 Camera::getProjectionMatrix() const {
    return glm::perspective(glm::radians(fov_deg), aspect_ratio, near_z, far_z);
}

void Camera::updateArcball(const glm::vec3 &lastMouseVec, const glm::vec3 &currentMouseVec) {
    float angle = std::acos(std::min(1.0f, glm::dot(lastMouseVec, currentMouseVec)));
    glm::vec3 axis = glm::cross(lastMouseVec, currentMouseVec);

    if (glm::length(axis) > 1e-5f) {
        axis = glm::normalize(axis);
        glm::quat deltaRot = glm::angleAxis(angle, axis);

        cameraOrientation = deltaRot * cameraOrientation;
        cameraOrientation = glm::normalize(cameraOrientation);
    }
}

glm::vec3 Camera::getArcballVector(int x, int y, int width, int height) {
    glm::vec3 P = glm::vec3(
        (2.0f * x - width) / (float)width,
        (width - 2.0f * y) / (float)height,
        0.0f
    );

    float OP_squared = P.x * P.x + P.y * P.y;
    if (OP_squared <= 1.0f) {
        P.z = std::sqrt(1.0f - OP_squared);
    } else {
        P = glm::normalize(P);
    }
    return P;
}

