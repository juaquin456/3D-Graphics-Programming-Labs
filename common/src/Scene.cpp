#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "common/Scene.h"
#include "common/MeshData.h"

#include <filesystem>
#include <iostream>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
std::string replaceCharacters(const std::string& s, char c1, char c2)
{
    std::string tmp(s);
    for (char& ch : tmp) {
        if (ch == c1)
            ch = c2;
        else if (ch == c2)
            ch = c1;
    }
    return tmp;
}
Scene Scene::loadOBJ(const std::string& filepath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::string baseDir = std::filesystem::path(filepath).parent_path().string();
    if (!baseDir.empty()) baseDir += "/";

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                                filepath.c_str(), baseDir.c_str(),
                                /*triangulate=*/true);

    if (!warn.empty()) std::cout << "[TinyOBJ] Warning: " << warn << std::endl;
    if (!err.empty())  std::cerr << "[TinyOBJ] Error: "   << err  << std::endl;
    if (!ret) {
        std::cerr << "[Scene] Failed to load OBJ: " << filepath << std::endl;
        return {};
    }

    std::cout << "[Scene] Loaded " << filepath
              << " — " << shapes.size()    << " shape(s), "
              << materials.size() << " material(s), "
              << attrib.vertices.size() / 3 << " vertex position(s)" << std::endl;

    Scene scene;

    std::unordered_map<std::string, Texture::Ptr> textureCache;
    auto loadTexture = [&](const std::string& texName) -> Texture::Ptr {
        if (texName.empty()) return nullptr;
        std::string fullPath = baseDir + texName;
        auto it = textureCache.find(fullPath);
        if (it != textureCache.end()) return it->second;
        auto tex = std::make_shared<Texture>(fullPath);
        textureCache[fullPath] = tex;
        return tex;
    };

    auto defaultMaterial = PhongMaterial::WhitePlastic();
    std::vector<IMaterial::Ptr> matPtrs;
    for (const auto& mat : materials) {
        glm::vec3 ke(mat.emission[0], mat.emission[1], mat.emission[2]);
        bool hasTextures = !mat.diffuse_texname.empty()  ||
                           !mat.specular_texname.empty() ||
                           !mat.bump_texname.empty();
        float shiny = mat.shininess >= 1.0f ? mat.shininess : 32.0f;
        if (hasTextures) {
            auto diffTex = !mat.diffuse_texname.empty()
                ? loadTexture(replaceCharacters(mat.diffuse_texname, '\\', '/'))
                : Texture::White();

            auto specTex = !mat.specular_texname.empty()
                ? loadTexture(replaceCharacters(mat.specular_texname, '\\', '/'))
                : Texture::White();

            auto normTex = !mat.bump_texname.empty()
                ? loadTexture(replaceCharacters(mat.bump_texname, '\\', '/'))
                : nullptr;
            auto texMat = std::make_shared<TexturedMaterial>(diffTex, specTex, normTex, shiny, ke);
            matPtrs.push_back(texMat);
        } else {
            auto phongMat = std::make_shared<PhongMaterial>(
                glm::vec3(mat.ambient[0],  mat.ambient[1],  mat.ambient[2]),
                glm::vec3(mat.diffuse[0],  mat.diffuse[1],  mat.diffuse[2]),
                glm::vec3(mat.specular[0], mat.specular[1], mat.specular[2]),
                shiny,
                ke
            );
            std::cout << "Ka ";
            for (float i : mat.ambient) std::cout << i << " ";
            std::cout << std::endl;

            std::cout << "Kd ";
            for (float i : mat.diffuse) std::cout << i << " ";
            std::cout << std::endl;
            std::cout << "Ks ";
            for (float i : mat.specular) std::cout << i << " ";
            std::cout << std::endl;
            std::cout << "Shininess " << mat.shininess << std::endl;
            matPtrs.push_back(phongMat);
        }
    }

    for (const auto& shape : shapes) {
        const size_t numFaces = shape.mesh.num_face_vertices.size();

        std::vector<size_t> faceOffsets(numFaces);
        {
            size_t off = 0;
            for (size_t f = 0; f < numFaces; ++f) {
                faceOffsets[f] = off;
                off += shape.mesh.num_face_vertices[f];
            }
        }

        std::unordered_map<int, std::vector<size_t>> matGroups;
        for (size_t f = 0; f < numFaces; ++f) {
            int matId = shape.mesh.material_ids.empty() ? -1
                                                        : shape.mesh.material_ids[f];
            matGroups[matId].push_back(f);
        }

        for (const auto& [matId, faces] : matGroups) {
            MeshData meshData;
            std::unordered_map<std::string, int> uniqueVerts;

            for (size_t f : faces) {
                const int fv = shape.mesh.num_face_vertices[f];
                const size_t base = faceOffsets[f];

                for (int v = 0; v < fv; ++v) {
                    const tinyobj::index_t& idx = shape.mesh.indices[base + v];

                    std::string key = std::to_string(idx.vertex_index)   + "/" +
                                      std::to_string(idx.texcoord_index) + "/" +
                                      std::to_string(idx.normal_index);

                    auto it = uniqueVerts.find(key);
                    if (it != uniqueVerts.end()) {
                        meshData.indices.push_back(it->second);
                    } else {
                        int newIdx = static_cast<int>(meshData.vertices.size());

                        meshData.vertices.emplace_back(
                            attrib.vertices[3 * idx.vertex_index + 0],
                            attrib.vertices[3 * idx.vertex_index + 1],
                            attrib.vertices[3 * idx.vertex_index + 2]
                        );

                        if (idx.texcoord_index >= 0 &&
                            static_cast<size_t>(2 * idx.texcoord_index + 1) < attrib.texcoords.size()) {
                            meshData.uvs.emplace_back(
                                attrib.texcoords[2 * idx.texcoord_index + 0],
                                attrib.texcoords[2 * idx.texcoord_index + 1]
                            );
                        }

                        // Normal
                        if (idx.normal_index >= 0 &&
                            static_cast<size_t>(3 * idx.normal_index + 2) < attrib.normals.size()) {
                            meshData.normals.emplace_back(
                                attrib.normals[3 * idx.normal_index + 0],
                                attrib.normals[3 * idx.normal_index + 1],
                                attrib.normals[3 * idx.normal_index + 2]
                            );
                        }

                        uniqueVerts[key] = newIdx;
                        meshData.indices.push_back(newIdx);
                    }
                }
            }

            if (!meshData.uvs.empty() && meshData.uvs.size() != meshData.vertices.size()) {
                meshData.uvs.resize(meshData.vertices.size(), glm::vec2(0.0f));
            }

            if (meshData.normals.size() != meshData.vertices.size()) {
                meshData.recompute_normals();
            }

            meshData.recompute_tangents();

            auto meshAsset = std::make_shared<MeshAsset>(meshData);
            scene.m_meshAssets.push_back(meshAsset);

            IMaterial::Ptr mat = PhongMaterial::WhitePlastic();
            if (matId >= 0 && matId < static_cast<int>(matPtrs.size())) {
                mat = matPtrs[matId];
            }
            assert(mat != nullptr);
            scene.objects.emplace_back(meshAsset, mat);
        }
    }

    std::cout << "[Scene] Created " << scene.objects.size() << " render object(s)" << std::endl;
    return scene;
}


glm::mat4 Scene::getModelMatrix() const {
    glm::mat4 model(1.0f);
    model = glm::translate(model, position);
    model = glm::scale(model, scale);
    model = model * glm::mat4_cast(rotation);
    return model;
}

void Scene::rotateAxis(float angleDegrees, const glm::vec3& axis) {
    glm::quat delta = glm::angleAxis(glm::radians(angleDegrees), glm::normalize(axis));
    rotation = glm::normalize(delta * rotation);
}

void Scene::setRotationEuler(float pitchDeg, float yawDeg, float rollDeg) {
    rotation = glm::quat(glm::vec3(
        glm::radians(pitchDeg),
        glm::radians(yawDeg),
        glm::radians(rollDeg)
    ));
}

void Scene::drawSolid(const Shader& solidShader) const {
    for (const auto& obj : objects) {
        if (!obj.meshAsset) continue;
        if (!dynamic_cast<PhongMaterial*>(obj.material.get())) continue;

        obj.draw(solidShader);
    }
}

void Scene::drawTextured(const Shader& texturedShader) const {
    for (const auto& obj : objects) {
        if (!obj.meshAsset) continue;
        if (!dynamic_cast<TexturedMaterial*>(obj.material.get())) continue;

        obj.draw(texturedShader);
    }
}

void Scene::draw(const Shader& shader) const {
    glm::mat4 sceneModel = getModelMatrix();

    for (const auto& obj : objects) {
        if (!obj.meshAsset) continue;

        glm::mat4 model = sceneModel * obj.getModelMatrix();
        shader.setMat4("model", model);

        if (obj.material) obj.material->apply(shader);
        obj.meshAsset->draw();
    }
}

void Scene::drawGeometry(const Shader& shader) const {
    for (const auto& obj : objects) {
        obj.drawGeometry(shader);
    }
}
