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
    float speed{2.5f};

    Camera() = default;
    Camera(glm::vec3 eye, glm::vec3 target, float fov, float aspect);
    [[nodiscard]] glm::mat4 getViewMatrix() const;
    [[nodiscard]] glm::mat4 getProjectionMatrix() const;
    void rotateOrbit(float deltaX, float deltaY, float sensitivity);
    void bind(const Shader& shader) const;

    [[nodiscard]] glm::vec3 getFront() const {
        return glm::normalize(target - position);
    }

    [[nodiscard]] glm::vec3 getRight() const {
        return glm::normalize(glm::cross(getFront(), up));
    }

    void moveForward(float deltaTime) {
        glm::vec3 dir = getFront();

        glm::vec3 delta = dir * speed * deltaTime;
        position += delta;
        target += delta;
    }

    void moveBackward(float deltaTime) {
        glm::vec3 dir = getFront();
        glm::vec3 delta = dir * speed * deltaTime;
        position -= delta;
        target -= delta;
    }

    void moveLeft(float deltaTime) {
        glm::vec3 delta = getRight() * speed * deltaTime;
        position -= delta;
        target -= delta;
    }

    void moveRight(float deltaTime) {
        glm::vec3 delta = getRight() * speed * deltaTime;
        position += delta;
        target += delta;
    }
};

#endif //ANIMATION_CAMERA_H
