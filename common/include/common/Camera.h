//
// Created by juaquin-remon on 9/6/26.
//

#ifndef ANIMATION_CAMERA_H
#define ANIMATION_CAMERA_H

#include "Shader.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"

class Camera {
public:
    glm::vec3 position{0, 0.5, 2};
    glm::vec3 target{0, 0, 0};
    glm::vec3 up{0, 1, 0};

    float fov_deg{45};
    float aspect_ratio{1280./720};
    float near_z{0.1};
    float far_z{100};

    Camera() = default;
    Camera(glm::vec3 eye, glm::vec3 target, float fov, float aspect);
    [[nodiscard]] glm::mat4 getViewMatrix() const;
    [[nodiscard]] glm::mat4 getProjectionMatrix() const;
    void rotateOrbit(float deltaX, float deltaY, float sensitivity);
    void bind(const Shader& shader) const;

    void moveForward() {
        glm::vec3 delta = {0, 0, -0.01};
        position += delta;
        target += delta;
    }


    void moveBackward() {
        glm::vec3 delta = {0, 0, 0.01};
        position += delta;
        target += delta;
    }

    void moveLeft() {
        glm::vec3 delta = {-0.01, 0, 0};
        position += delta;
        target += delta;
    }
    void moveRight() {
        glm::vec3 delta = {0.01, 0, 0};
        position += delta;
        target += delta;
    }
};

#endif //ANIMATION_CAMERA_H
