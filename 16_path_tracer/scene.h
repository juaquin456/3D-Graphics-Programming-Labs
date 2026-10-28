#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cmath>

#include <tiny_obj_loader.h>
#include <glm/glm.hpp>

#include "SceneTypes.h"

struct PathSceneData {
    std::vector<PathTriangle> triangles;
    std::vector<PathMaterial> materials;
};

inline PathSceneData loadPathScene(const std::string& filepath) {
    PathSceneData data;
    tinyobj::ObjReaderConfig reader_config;
    tinyobj::ObjReader reader;

    std::filesystem::path p(filepath);
    std::string filename = p.filename().string();
    std::filesystem::path dir = p.parent_path();

    reader_config.mtl_search_path = dir.string();
    reader_config.triangulate = true;

    if (!reader.ParseFromFile(filepath, reader_config)) {
        if (!reader.Error().empty()) {
            std::cerr << "[PathSceneLoader] Error: " << reader.Error() << std::endl;
        }
        return data;
    }

    if (!reader.Warning().empty()) {
        std::cout << "[PathSceneLoader] Warning: " << reader.Warning() << std::endl;
    }

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();
    const auto& objMaterials = reader.GetMaterials();

    data.materials.reserve(objMaterials.size());
    for (const auto& mat : objMaterials) {
        PathMaterial pm{};

        std::string matName = mat.name;
        std::transform(matName.begin(), matName.end(), matName.begin(), ::tolower);

        glm::vec3 kd(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);
        glm::vec3 ks(mat.specular[0], mat.specular[1], mat.specular[2]);
        glm::vec3 ke(mat.emission[0], mat.emission[1], mat.emission[2]);

        float ksLen = glm::length(ks);
        float kdLen = glm::length(kd);

        pm.emission = ke;

        if (mat.ior > 1.01f || matName.find("glass") != std::string::npos || matName.find("dielectric") != std::string::npos) {
            pm.type   = MaterialType::Dielectric;
            pm.albedo = glm::vec3(1.0f);
            pm.ior    = (mat.ior > 1.0f) ? mat.ior : 1.5f;
        }
        else if (mat.illum == 3 || matName.find("mirror") != std::string::npos || matName.find("specular") != std::string::npos || (ksLen > 0.7f && ksLen >= kdLen)) {
            pm.type   = MaterialType::Specular;
            pm.albedo = (ksLen > 0.01f) ? ks : glm::vec3(0.95f);
        }
        else {
            pm.type   = MaterialType::Diffuse;
            pm.albedo = kd;
        }

        data.materials.push_back(pm);
    }

    if (data.materials.empty()) {
        PathMaterial defaultMat{};
        defaultMat.type   = MaterialType::Diffuse;
        defaultMat.albedo = glm::vec3(0.8f);
        data.materials.push_back(defaultMat);
    }

    for (const auto& shape : shapes) {
        size_t index_offset = 0;

        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            size_t fv = shape.mesh.num_face_vertices[f];
            int matId = shape.mesh.material_ids[f];

            if (matId < 0 || static_cast<size_t>(matId) >= data.materials.size()) {
                matId = 0;
            }

            if (fv == 3) {
                tinyobj::index_t idx0 = shape.mesh.indices[index_offset + 0];
                tinyobj::index_t idx1 = shape.mesh.indices[index_offset + 1];
                tinyobj::index_t idx2 = shape.mesh.indices[index_offset + 2];

                PathTriangle tri{};
                tri.materialId = static_cast<uint32_t>(matId);

                // Vértices
                tri.v0 = glm::vec3(attrib.vertices[3 * idx0.vertex_index + 0],
                                   attrib.vertices[3 * idx0.vertex_index + 1],
                                   attrib.vertices[3 * idx0.vertex_index + 2]);

                tri.v1 = glm::vec3(attrib.vertices[3 * idx1.vertex_index + 0],
                                   attrib.vertices[3 * idx1.vertex_index + 1],
                                   attrib.vertices[3 * idx1.vertex_index + 2]);

                tri.v2 = glm::vec3(attrib.vertices[3 * idx2.vertex_index + 0],
                                   attrib.vertices[3 * idx2.vertex_index + 1],
                                   attrib.vertices[3 * idx2.vertex_index + 2]);

                bool hasNormals = (idx0.normal_index >= 0 && idx1.normal_index >= 0 && idx2.normal_index >= 0);

                if (hasNormals) {
                    tri.n0 = glm::vec3(attrib.normals[3 * idx0.normal_index + 0],
                                       attrib.normals[3 * idx0.normal_index + 1],
                                       attrib.normals[3 * idx0.normal_index + 2]);

                    tri.n1 = glm::vec3(attrib.normals[3 * idx1.normal_index + 0],
                                       attrib.normals[3 * idx1.normal_index + 1],
                                       attrib.normals[3 * idx1.normal_index + 2]);

                    tri.n2 = glm::vec3(attrib.normals[3 * idx2.normal_index + 0],
                                       attrib.normals[3 * idx2.normal_index + 1],
                                       attrib.normals[3 * idx2.normal_index + 2]);
                } else {
                    glm::vec3 fn = glm::normalize(glm::cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
                    tri.n0 = tri.n1 = tri.n2 = fn;
                }

                data.triangles.push_back(tri);
            }

            index_offset += fv;
        }
    }

    std::cout << "[PathSceneLoader] Cargados " << data.triangles.size() << " triángulos y "
              << data.materials.size() << " materiales directamente desde " << filename << std::endl;

    return data;
}

PathSceneData createProceduralCornellBox() {
    PathSceneData data;

    PathMaterial white{ MaterialType::Diffuse, {0.725f, 0.71f, 0.68f} };
    PathMaterial red  { MaterialType::Diffuse, {0.63f, 0.065f, 0.05f} };
    PathMaterial green{ MaterialType::Diffuse, {0.14f, 0.45f, 0.091f} };
    PathMaterial light{ MaterialType::Diffuse, {1.0f, 1.0f, 1.0f}, {15.0f, 15.0f, 15.0f} }; // Emisor
    PathMaterial mirror{ MaterialType::Specular, {0.99f, 0.99f, 0.99f} };
    PathMaterial glass { MaterialType::Dielectric, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, 1.5f };

    data.materials = { white, red, green, light, mirror, glass };

    auto addQuad = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, uint32_t matId) {
        glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        data.triangles.push_back({ p0, p1, p2, n, n, n, matId });
        data.triangles.push_back({ p0, p2, p3, n, n, n, matId });
    };

    addQuad({-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1}, 0); // Fondo (Blanco)
    addQuad({-1,-1,-1}, {-1, 1,-1}, {-1, 1, 1}, {-1,-1, 1}, 1); // Izquierda (Rojo)
    addQuad({ 1,-1,-1}, { 1,-1, 1}, { 1, 1, 1}, { 1, 1,-1}, 2); // Derecha (Verde)
    addQuad({-1,-1,-1}, {-1,-1, 1}, { 1,-1, 1}, { 1,-1,-1}, 0); // Piso (Blanco)
    addQuad({-1, 1,-1}, { 1, 1,-1}, { 1, 1, 1}, {-1, 1, 1}, 0); // Techo (Blanco)

    addQuad({-0.25f, 0.99f, -0.25f}, {-0.25f, 0.99f, 0.25f}, {0.25f, 0.99f, 0.25f}, {0.25f, 0.99f, -0.25f}, 3);

    return data;
}