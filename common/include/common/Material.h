//
// Created by juaquin-remon on 9/20/26.
//

#ifndef TEXTURES_MATERIAL_H
#define TEXTURES_MATERIAL_H
#include <memory>
#include <utility>

#include "Shader.h"
#include "Texture.h"

class IMaterial {
public:
    using Ptr = std::shared_ptr<IMaterial>;
    virtual ~IMaterial() = default;

    virtual void apply(const Shader& shader) const = 0;
};

class PhongMaterial : public IMaterial {
public:
    glm::vec3 Ka{0.2f};
    glm::vec3 Kd{0.8f, 0.3f, 0.2f};
    glm::vec3 Ks{1.0f};
    glm::vec3 Ke{0};
    float shininess{32.0f};

    PhongMaterial() = default;
    PhongMaterial(glm::vec3 ka, glm::vec3 kd, glm::vec3 ks, float shiny, glm::vec3 ke = glm::vec3(0.0f))
            : Ka(ka), Kd(kd), Ks(ks), Ke(ke), shininess(shiny) {}

    void apply(const Shader& shader) const override {
        shader.setVec3("material.Ka", Ka);
        shader.setVec3("material.Kd", Kd);
        shader.setVec3("material.Ks", Ks);
        shader.setVec3("material.Ke", Ks);
        shader.setFloat("material.shininess", shininess);
    }

    static std::shared_ptr<PhongMaterial> Gold() {
        return std::make_shared<PhongMaterial>(
            glm::vec3(0.24, 0.19, 0.07),
            glm::vec3(0.75, 0.6, 0.22),
            glm::vec3(0.62, 0.55, 0.36),
            51.2f
        );
    }

    static std::shared_ptr<PhongMaterial> WhitePlastic() {
        return std::make_shared<PhongMaterial>(
            glm::vec3(0.05f, 0.05f, 0.05f),
            glm::vec3(0.85f, 0.85f, 0.85f),
            glm::vec3(0.7f, 0.7f, 0.7f),
            32.0f
        );
    }
    static std::shared_ptr<PhongMaterial> BlackPlastic() {
        return std::make_shared<PhongMaterial>(
            glm::vec3(0.),
            glm::vec3(0.01),
            glm::vec3(0.5),
            32.0f
        );
    }
};
class TexturedMaterial : public IMaterial {
public:
    Texture::Ptr diffuseMap;
    Texture::Ptr specularMap;
    Texture::Ptr normalMap;
    glm::vec3 Ke{0};
    float shininess{32.0f};

    void apply(const Shader& shader) const override {
        shader.setFloat("material.shininess", shininess);
        shader.setVec3("material.Ke", Ke);

        if (diffuseMap) {
            diffuseMap->bind(0);
            shader.setInt("material.diffuseMap", 0);
        }
        if (specularMap) {
            specularMap->bind(1);
            shader.setInt("material.specularMap", 1);
        }
        if (normalMap) {
            normalMap->bind(2);
            shader.setInt("material.normalMap", 2);
            shader.setBool("material.hasNormalMap", true);
        } else {
            shader.setBool("material.hasNormalMap", false);
        }
    }

    TexturedMaterial(const std::shared_ptr<Texture>& diffuse,
                      const std::shared_ptr<Texture>& specular,
                      Texture::Ptr norm = nullptr,
                      float shiny = 32.0f,
                      glm::vec3 ke = glm::vec3(0.0f))
         : diffuseMap(diffuse), specularMap(specular), normalMap(std::move(norm)), Ke(ke), shininess(shiny) {}
};
#endif //TEXTURES_MATERIAL_H