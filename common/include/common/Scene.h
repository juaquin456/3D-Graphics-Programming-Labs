#ifndef COMMON_SCENE_H
#define COMMON_SCENE_H

#include <string>
#include <vector>
#include <memory>
#include "RenderObject.h"

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};

    Ray() = default;
    Ray(const glm::vec3& orig, const glm::vec3& dir)
        : origin(orig), direction(glm::normalize(dir)) {}

    glm::vec3 at(float t) const {
        return origin + t * direction;
    }

    static Ray GetMouseRay(double mouseX, double mouseY,
                           int screenWidth, int screenHeight,
                           const glm::mat4& viewMatrix,
                           const glm::mat4& projectionMatrix)
    {
        float ndcX = (2.0f * static_cast<float>(mouseX)) / static_cast<float>(screenWidth) - 1.0f;
        float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY)) / static_cast<float>(screenHeight);

        glm::vec4 clipCoords = glm::vec4(ndcX, ndcY, -1.0f, 1.0f);

        glm::vec4 eyeCoords = glm::inverse(projectionMatrix) * clipCoords;
        eyeCoords = glm::vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f);

        glm::vec3 rayDirection = glm::vec3(glm::inverse(viewMatrix) * eyeCoords);
        rayDirection = glm::normalize(rayDirection);

        glm::vec3 rayOrigin = glm::vec3(glm::inverse(viewMatrix)[3]);

        return Ray(rayOrigin, rayDirection);
    }
};

struct PickResult {
    bool hit{false};
    float distance{1e9f};
    int vertexIndex{-1};
    glm::vec3 vertexWorldPos{0.0f};
    std::shared_ptr<MeshAsset> meshAsset{nullptr};
};

bool rayTriangleIntersect(const Ray& ray, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                         float& outT, glm::vec3& outBarycentric);
PickResult pickLocalVertex(const Ray& worldRay, const MeshData& mesh, const glm::mat4& modelMatrix);

class Scene {
public:
    std::vector<RenderObject> objects;

    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    Scene() = default;

    static Scene loadOBJ(const std::string& filepath);

    [[nodiscard]] glm::mat4 getModelMatrix() const;

    void draw(const Shader& shader) const;
    void drawSolid(const Shader& shader) const;
    void drawTextured(const Shader& shader) const;

    void drawGeometry(const Shader& shader) const;

    void rotateAxis(float angleDegrees, const glm::vec3& axis);
    void setRotationEuler(float pitchDeg, float yawDeg, float rollDeg);
    [[nodiscard]] PickResult pickVertex(const Ray& worldRay) const;

private:
    std::vector<MeshAsset::Ptr> m_meshAssets;
};

#endif // COMMON_SCENE_H
