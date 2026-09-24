#ifndef COMMON_SCENE_H
#define COMMON_SCENE_H

#include <string>
#include <vector>
#include <memory>
#include "RenderObject.h"


class Scene {
public:
    std::vector<RenderObject> objects;

    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    Scene() = default;

    static Scene loadOBJ(const std::string& filepath);

    [[nodiscard]] glm::mat4 getModelMatrix() const;

    void draw(const Shader& solidShader, const Shader& texturedShader) const;

    void draw(const Shader& shader) const;

    void drawGeometry(const Shader& shader) const;

    void rotateAxis(float angleDegrees, const glm::vec3& axis);
    void setRotationEuler(float pitchDeg, float yawDeg, float rollDeg);

private:
    std::vector<MeshAsset::Ptr> m_meshAssets;
    std::vector<Texture::Ptr>   m_textures;
    std::vector<IMaterial::Ptr> m_materials;
};

#endif // COMMON_SCENE_H
