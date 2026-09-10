//
// Created by juaquin-remon on 9/6/26.
//

#ifndef ANIMATION_RENDEROBJECT_H
#define ANIMATION_RENDEROBJECT_H
#include <glm/gtc/quaternion.hpp>
#include <utility>
#include "MeshAsset.h"
#include "Shader.h"
#include "glm/fwd.hpp"

struct Material {
    glm::vec3 Ka{0.2f, 0.2f, 0.2f};
    glm::vec3 Kd{0.8f, 0.3f, 0.2f};
    glm::vec3 Ks{1.0f, 1.0f, 1.0f};
    float shininess{32.0f};

   static Material RedPlastic() {
        return {
            glm::vec3(0.05f, 0.0f,  0.0f),
            glm::vec3(0.5f,  0.0f,  0.0f),
            glm::vec3(0.7f,  0.6f,  0.6f),
            32.0f
        };
    }

    static Material CyanPlastic() {
        return {
            glm::vec3(0.0f,  0.1f, 0.06f),
            glm::vec3(0.0f,  0.5098039f, 0.5098039f),
            glm::vec3(0.50196078f, 0.50196078f, 0.50196078f),
            32.0f
        };
    }

    static Material Gold() {
        return {
            glm::vec3(0.24725f, 0.1995f, 0.0745f),
            glm::vec3(0.75164f, 0.60648f, 0.22648f),
            glm::vec3(0.628281f, 0.555802f, 0.366065f),
            51.2f
        };
    }

    static Material Silver() {
        return {
            glm::vec3(0.19225f, 0.19225f, 0.19225f),
            glm::vec3(0.50754f, 0.50754f, 0.50754f),
            glm::vec3(0.508273f, 0.508273f, 0.508273f),
            51.2f
        };
    }

    static Material Bronze() {
        return {
            glm::vec3(0.2125f, 0.1275f, 0.054f),
            glm::vec3(0.714f, 0.4284f, 0.18144f),
            glm::vec3(0.393548f, 0.271906f, 0.166721f),
            25.6f
        };
    }

    static Material Emerald() {
        return {
            glm::vec3(0.0215f, 0.1745f, 0.0215f),
            glm::vec3(0.07568f, 0.61424f, 0.07568f),
            glm::vec3(0.633f, 0.727811f, 0.633f),
            76.8f
        };
    }

    static Material Jade() {
        return {
            glm::vec3(0.135f, 0.2225f, 0.1575f),
            glm::vec3(0.54f, 0.89f, 0.63f),
            glm::vec3(0.316228f, 0.316228f, 0.316228f),
            12.8f
        };
    }

    static Material Chalk() {
        return {
            glm::vec3(0.1f, 0.1f, 0.1f),
            glm::vec3(0.8f, 0.8f, 0.8f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            1.0f
        };
    }
};

class RenderObject {
public:
    MeshAsset::Ptr meshAsset;
    Material material;

    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 color{1.0f};

    explicit RenderObject(MeshAsset::Ptr asset) : meshAsset(std::move(asset)) {}
    void rotateAxis(float angleDegrees, const glm::vec3& axis);
    void setRotationEuler(float pitchDeg, float yawDeg, float rollDeg);
    [[nodiscard]] glm::mat4 getModelMatrix() const;

    void draw(const Shader& shader) const;
};
#endif //ANIMATION_RENDEROBJECT_H