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

