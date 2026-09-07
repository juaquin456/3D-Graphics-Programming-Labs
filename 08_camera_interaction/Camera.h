//
// Created by juaquin-remon on 9/7/26.
//

#ifndef ANIMATION_CAMERA_H
#define ANIMATION_CAMERA_H
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
class Camera {
public:
    glm::quat cameraOrientation{1.0f, 0.0f, 0.0f, 0.0f};
    float cameraDistance{2.5f};

    float fov_deg{45.0f};
    float aspect_ratio{1280.0f / 720.0f};
    float near_z{0.1f};
    float far_z{100.0f};

    Camera() = default;

    static glm::vec3 getArcballVector(int x, int y, int width, int height) {
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

    void updateArcball(const glm::vec3& lastMouseVec, const glm::vec3& currentMouseVec) {
        float angle = std::acos(std::min(1.0f, glm::dot(lastMouseVec, currentMouseVec)));
        glm::vec3 axis = glm::cross(lastMouseVec, currentMouseVec);

        if (glm::length(axis) > 1e-5f) {
            axis = glm::normalize(axis);
            glm::quat deltaRot = glm::angleAxis(angle, axis);

            cameraOrientation = deltaRot * cameraOrientation;
            cameraOrientation = glm::normalize(cameraOrientation);
        }
    }

    [[nodiscard]] glm::mat4 getViewMatrix() const {
        glm::mat4 rotationMatrix = glm::mat4_cast(cameraOrientation);
        glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -cameraDistance));

        return translationMatrix * rotationMatrix;
    }

    [[nodiscard]] glm::mat4 getProjectionMatrix() const {
        return glm::perspective(glm::radians(fov_deg), aspect_ratio, near_z, far_z);
    }
};
#endif //ANIMATION_CAMERA_H