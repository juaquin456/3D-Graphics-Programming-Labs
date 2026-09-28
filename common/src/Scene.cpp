#define TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_USE_MAP_BOX_PARSER
#include "tiny_obj_loader.h"

#include "common/Scene.h"
#include "common/MeshData.h"

#include <filesystem>
#include <iostream>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
class TextureCache {
public:
    static std::shared_ptr<Texture> get(const std::string& filepath) {
        static std::unordered_map<std::string, std::weak_ptr<Texture>> cache;

        auto it = cache.find(filepath);
        if (it != cache.end()) {
            if (auto tex = it->second.lock()) {
                return tex;
            }
        }

        std::cout << "[TextureCache] Loading new texture from disk: " << filepath << std::endl;
        auto newTex = std::make_shared<Texture>(filepath);
        cache[filepath] = newTex;
        return newTex;
    }
};
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

bool rayTriangleIntersect(const Ray &ray, const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2, float &outT,
    glm::vec3 &outBarycentric) {
    const float EPSILON = 1e-7f;
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 h = glm::cross(ray.direction, edge2);
    float a = glm::dot(edge1, h);

    if (a > -EPSILON && a < EPSILON) return false;

    float f = 1.0f / a;
    glm::vec3 s = ray.origin - v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(ray.direction, q);
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = f * glm::dot(edge2, q);
    if (t > EPSILON) {
        outT = t;
        outBarycentric = glm::vec3(1.0f - u - v, u, v);
        return true;
    }
    return false;
}

PickResult pickLocalVertex(const Ray &worldRay, const MeshData &mesh, const glm::mat4 &modelMatrix) {
    PickResult bestPick;

    glm::mat4 invModel = glm::inverse(modelMatrix);
    Ray localRay;
    localRay.origin = glm::vec3(invModel * glm::vec4(worldRay.origin, 1.0f));
    localRay.direction = glm::normalize(glm::vec3(invModel * glm::vec4(worldRay.direction, 0.0f)));

    const size_t numTriangles = mesh.indices.size() / 3;

    #pragma omp parallel
    {
        PickResult localBest;

        #pragma omp for nowait
        for (size_t f = 0; f < numTriangles; ++f) {
            size_t i = f * 3;
            int idx0 = mesh.indices[i];
            int idx1 = mesh.indices[i + 1];
            int idx2 = mesh.indices[i + 2];

            const glm::vec3& l0 = mesh.vertices[idx0];
            const glm::vec3& l1 = mesh.vertices[idx1];
            const glm::vec3& l2 = mesh.vertices[idx2];

            float t;
            glm::vec3 barycentric;
            if (rayTriangleIntersect(localRay, l0, l1, l2, t, barycentric)) {
                if (t < localBest.distance) {
                    localBest.hit = true;
                    localBest.distance = t;

                    if (barycentric.x >= barycentric.y && barycentric.x >= barycentric.z) {
                        localBest.vertexIndex = idx0;
                        localBest.vertexWorldPos = glm::vec3(modelMatrix * glm::vec4(l0, 1.0f));
                    } else if (barycentric.y >= barycentric.x && barycentric.y >= barycentric.z) {
                        localBest.vertexIndex = idx1;
                        localBest.vertexWorldPos = glm::vec3(modelMatrix * glm::vec4(l1, 1.0f));
                    } else {
                        localBest.vertexIndex = idx2;
                        localBest.vertexWorldPos = glm::vec3(modelMatrix * glm::vec4(l2, 1.0f));
                    }
                }
            }
        }

        #pragma omp critical
        {
            if (localBest.hit && localBest.distance < bestPick.distance) {
                bestPick = localBest;
            }
        }
    }

    return bestPick;
}

Scene Scene::loadOBJ(const std::string& filepath) {
    Scene scene;
    tinyobj::ObjReaderConfig reader_config;
    tinyobj::ObjReader reader;

    std::filesystem::path p(filepath);
    std::string filename = p.filename().string();
    std::filesystem::path dir = p.parent_path();
    std::string mtl_search_path = dir.string();

    std::cout << "[Scene] Target OBJ: " << filename << std::endl;
    std::cout << "[Scene] MTL search path: " << mtl_search_path << std::endl;

    reader_config.mtl_search_path = mtl_search_path;
    reader_config.triangulate = true;

    if (!reader.ParseFromFile(filepath, reader_config)) {
        if (!reader.Error().empty()) {
            std::cerr << "TinyObjLoader Error: " << reader.Error() << std::endl;
        }
        return scene;
    }

    if (!reader.Warning().empty()) {
        std::cout << "TinyObjLoader Warning: " << reader.Warning() << std::endl;
    }

    auto& attrib = reader.GetAttrib();
    auto& shapes = reader.GetShapes();
    auto& materials = reader.GetMaterials();

    std::cout << "[Scene] Num shapes: " << shapes.size() << std::endl;
    std::cout << "[Scene] Num materials: " << materials.size() << std::endl;

    std::vector<std::shared_ptr<IMaterial>> loadedMaterials;
    for (const auto& mat : materials) {
        if (!mat.diffuse_texname.empty()) {
            std::string diffPath = (dir / replaceCharacters(mat.diffuse_texname, '\\', '/')).string();
            auto diffTex = TextureCache::get(diffPath);

            std::shared_ptr<Texture> specTex = nullptr;
            if (!mat.specular_texname.empty()) {
                std::string specPath = (dir / replaceCharacters(mat.specular_texname, '\\', '/')).string();
                specTex = TextureCache::get(specPath);
            }

            std::shared_ptr<Texture> normTex = nullptr;
            if (!mat.bump_texname.empty()) {
                std::string normPath = (dir / replaceCharacters(mat.bump_texname, '\\', '/')).string();
                normTex = TextureCache::get(normPath);
            }

            auto texMat = std::make_shared<TexturedMaterial>(diffTex, specTex, normTex);
            texMat->Ke = glm::vec3(mat.emission[0], mat.emission[1], mat.emission[2]);
            texMat->shininess = (mat.shininess > 0.0f) ? mat.shininess : 32.0f;
            loadedMaterials.push_back(texMat);
        } else {
            auto phongMat = std::make_shared<PhongMaterial>();
            phongMat->Ka = glm::vec3(mat.ambient[0], mat.ambient[1], mat.ambient[2]);
            phongMat->Kd = glm::vec3(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);
            phongMat->Ks = glm::vec3(mat.specular[0], mat.specular[1], mat.specular[2]);
            phongMat->Ke = glm::vec3(mat.emission[0], mat.emission[1], mat.emission[2]);
            phongMat->shininess = (mat.shininess > 0.0f) ? mat.shininess : 32.0f;
            loadedMaterials.push_back(phongMat);
        }
    }

    const size_t total_positions = attrib.vertices.size() / 3;

    for (const auto& shape : shapes) {
        std::unordered_map<int, MeshData> matToMesh;
        std::unordered_map<int, std::vector<int>> matToUniqueVerts;

        size_t total_indices = shape.mesh.indices.size();

        size_t index_offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            size_t fv = shape.mesh.num_face_vertices[f];
            int matId = shape.mesh.material_ids[f];

            MeshData& meshData = matToMesh[matId];
            auto& uniqueVerts = matToUniqueVerts[matId];

            if (uniqueVerts.empty()) {
                uniqueVerts.assign(total_positions, -1);
                meshData.vertices.reserve(total_positions);
                meshData.indices.reserve(total_indices);
                meshData.uvs.reserve(total_positions);
                meshData.normals.reserve(total_positions);
            }

            for (size_t v = 0; v < fv; v++) {
                tinyobj::index_t idx = shape.mesh.indices[index_offset + v];

                int posKey = idx.vertex_index;

                int existingIdx = uniqueVerts[posKey];
                if (existingIdx != -1) {
                    meshData.indices.push_back(existingIdx);
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
                    } else {
                        meshData.uvs.emplace_back(0.0f, 0.0f);
                    }

                    if (idx.normal_index >= 0 &&
                        static_cast<size_t>(3 * idx.normal_index + 2) < attrib.normals.size()) {
                        meshData.normals.emplace_back(
                            attrib.normals[3 * idx.normal_index + 0],
                            attrib.normals[3 * idx.normal_index + 1],
                            attrib.normals[3 * idx.normal_index + 2]
                        );
                    } else {
                        meshData.normals.emplace_back(0.0f, 1.0f, 0.0f);
                    }

                    uniqueVerts[posKey] = newIdx;
                    meshData.indices.push_back(newIdx);
                }
            }
            index_offset += fv;
        }

        for (auto& pair : matToMesh) {
            int matId = pair.first;
            MeshData& meshData = pair.second;

            if (meshData.vertices.empty()) continue;

            if (meshData.normals.empty()) {
                meshData.recompute_normals();
            }

            auto meshAsset = std::make_shared<MeshAsset>(meshData);
            scene.m_meshAssets.push_back(meshAsset);

            std::shared_ptr<IMaterial> mat = nullptr;
            if (matId >= 0 && static_cast<size_t>(matId) < loadedMaterials.size()) {
                mat = loadedMaterials[matId];
            } else {
                mat = PhongMaterial::WhitePlastic();
            }

            RenderObject obj(meshAsset, mat);
            scene.objects.push_back(obj);
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

PickResult Scene::pickVertex(const Ray &worldRay) const {
    PickResult closestPick;

    for (const auto& obj : objects) {
        if (!obj.meshAsset) continue;

        PickResult res = pickLocalVertex(worldRay, obj.meshAsset->mesh, obj.getModelMatrix());

        if (res.hit && res.distance < closestPick.distance) {
            closestPick = res;
            closestPick.meshAsset = obj.meshAsset;
        }
    }

    return closestPick;
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
