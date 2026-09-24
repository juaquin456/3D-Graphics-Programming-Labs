//
// Created by juaquin-remon on 9/6/26.
//

#ifndef ANIMATION_RENDEROBJECT_H
#define ANIMATION_RENDEROBJECT_H
#include <utility>

#include "Material.h"
#include "MeshAsset.h"
#include "Shader.h"
#include "Texture.h"


class RenderObject {
public:
    MeshAsset::Ptr meshAsset;
    IMaterial::Ptr material;

    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 color{1.0f};

    explicit RenderObject(MeshAsset::Ptr asset, IMaterial::Ptr mat = nullptr) : meshAsset(std::move(asset)), material(std::move(mat)) {
    }

    void rotateAxis(float angleDegrees, const glm::vec3& axis);
    void setRotationEuler(float pitchDeg, float yawDeg, float rollDeg);
    [[nodiscard]] glm::mat4 getModelMatrix() const;

    void draw(const Shader& shader) const;
    void drawGeometry(const Shader& shader) const;
};

struct DirectionalLight {
    glm::vec3 position{2, 4, 1};
    glm::vec3 target{0, 0, 0};

    glm::vec3 ambient{0.2, 0.2, 0.2};
    glm::vec3 diffuse{0.5, 0.5, 0.5};
    glm::vec3 specular{1, 1, 1};

    [[nodiscard]] glm::mat4 getLightSpaceMatrix(float orthoSize = 10, float nearPlane = 1, float farPlane = 20) const;
    void bind(const Shader& shader) const;
};
#endif //ANIMATION_RENDEROBJECT_H