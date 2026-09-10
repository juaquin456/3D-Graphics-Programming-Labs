#include "common/MeshData.h"
#include "happly.h"
#include <array>
#include <cmath>

#include "glm/geometric.hpp"

static constexpr float PI = 3.14159265358979323846f;

MeshData readPly(const std::string& filename) {
    MeshData m;
    happly::PLYData ply_in(filename);
    std::vector<std::array<double, 3>> v_pos = ply_in.getVertexPositions();
    m.vertices.reserve(v_pos.size());
    for (const auto& p : v_pos) {
        m.vertices.emplace_back(p[0], p[1], p[2]);
    }

    std::vector<std::vector<size_t>> f_ind = ply_in.getFaceIndices<size_t>();
    m.indices.reserve(f_ind.size() * 3);
    for (const auto& f : f_ind) {
        if (f.size() >= 3) {
            m.indices.push_back(static_cast<int>(f[0]));
            m.indices.push_back(static_cast<int>(f[1]));
            m.indices.push_back(static_cast<int>(f[2]));
        }
    }
    return m;
}

void MeshData::save(const std::string& filename) const {
    happly::PLYData ply_out;

    std::vector<std::array<double, 3>> out_vertices(vertices.size());
    for (size_t i = 0; i < vertices.size(); i++) {
        out_vertices[i / 3] = {vertices[i].x, vertices[i].y, vertices[i].z};
    }

    std::vector<std::vector<size_t>> out_face_indices(indices.size() / 3);
    for (size_t i = 0; i < indices.size(); i += 3) {
        out_face_indices[i / 3] = {static_cast<size_t>(indices[i]),
                                   static_cast<size_t>(indices[i + 1]),
                                   static_cast<size_t>(indices[i + 2])};
    }

    ply_out.addVertexPositions(out_vertices);
    ply_out.addFaceIndices(out_face_indices);

    ply_out.write(filename, happly::DataFormat::ASCII);
}

std::pair<glm::vec3, glm::vec3> MeshData::bounding_box() const {
    float minx = std::numeric_limits<float>::max();
    float miny = std::numeric_limits<float>::max();
    float minz = std::numeric_limits<float>::max();
    float maxx = std::numeric_limits<float>::lowest();
    float maxy = std::numeric_limits<float>::lowest();
    float maxz = std::numeric_limits<float>::lowest();

#pragma omp simd
    for (int i = 0; i < vertices.size(); i++) {
        minx = std::min(minx, vertices[i].x);
        miny = std::min(miny, vertices[i].y);
        minz = std::min(minz, vertices[i].z);
        maxx = std::max(maxx, vertices[i].x);
        maxy = std::max(maxy, vertices[i].y);
        maxz = std::max(maxz, vertices[i].z);
    }
    return {{minx, miny, minz}, {maxx, maxy, maxz}};
}

void MeshData::recompute_normals() {
    size_t num_vertices = vertices.size();
    normals.assign(num_vertices, glm::vec3(0.0f));

    for (size_t i = 0; i < indices.size(); i += 3) {
        int idx0 = indices[i];
        int idx1 = indices[i + 1];
        int idx2 = indices[i + 2];

        glm::vec3 v0 = vertices[idx0];
        glm::vec3 v1 = vertices[idx1];
        glm::vec3 v2 = vertices[idx2];

        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 face_normal = glm::cross(edge1, edge2);

        normals[idx0] += face_normal;
        normals[idx1] += face_normal;
        normals[idx2] += face_normal;
    }

    for (size_t i = 0; i < num_vertices; ++i) {
        float len = glm::length(normals[i]);
        if (len > 1e-8f) {
            normals[i] /= len;
        } else {
            normals[i] = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
}

MeshData NewSphere(float radius, int slices, int stacks) {
    MeshData m;

    m.vertices.emplace_back(0.0f, 0, radius);

    for (int i = 1; i < stacks; i++) {
        float a = PI * i / stacks;
        float sin_a = std::sin(a);
        float cos_a = std::cos(a);

        for (int j = 0; j < slices; j++) {
            float b = 2.0f * PI * j / slices;
            float x = radius * sin_a * std::cos(b);
            float y = radius * sin_a * std::sin(b);
            float z = radius * cos_a;

            m.vertices.emplace_back(x, y, z);
        }
    }

    m.vertices.emplace_back(0, 0, -radius);

    int south_pole_idx = static_cast<int>(m.vertices.size()) - 1;

    for (int j = 0; j < slices; j++) {
        int current = 1 + j;
        int next_j = 1 + (j + 1) % slices;

        m.indices.push_back(0);
        m.indices.push_back(next_j);
        m.indices.push_back(current);
    }

    for (int i = 0; i < stacks - 2; i++) {
        int ring1 = 1 + i * slices;
        int ring2 = 1 + (i + 1) * slices;

        for (int j = 0; j < slices; j++) {
            int next_j = (j + 1) % slices;

            int u0 = ring1 + j;
            int u1 = ring1 + next_j;
            int v0 = ring2 + j;
            int v1 = ring2 + next_j;

            m.indices.push_back(u0);
            m.indices.push_back(v0);
            m.indices.push_back(u1);

            m.indices.push_back(u1);
            m.indices.push_back(v0);
            m.indices.push_back(v1);
        }
    }

    int last_ring = 1 + (stacks - 2) * slices;
    for (int j = 0; j < slices; j++) {
        int current = last_ring + j;
        int next_j = last_ring + (j + 1) % slices;

        m.indices.push_back(current);
        m.indices.push_back(next_j);
        m.indices.push_back(south_pole_idx);
    }

    return m;
}

MeshData NewCube(float size) {
    MeshData m;
    float h = size * 0.5f;

    m.vertices = {
        // Front
        glm::vec3{-h, -h,  h},
        glm::vec3{h, -h,  h},
        glm::vec3{h,  h,  h},
        glm::vec3{-h,  h,  h},
        // Back
        glm::vec3{-h, -h, -h},
        glm::vec3{h, -h, -h},
        glm::vec3{h,  h, -h},
        glm::vec3{-h,  h, -h}
    };

    m.indices = {
        // Front
        0, 1, 2,  2, 3, 0,
        // Right
        1, 5, 6,  6, 2, 1,
        // Back
        5, 4, 7,  7, 6, 5,
        // Left
        4, 0, 3,  3, 7, 4,
        // Top
        3, 2, 6,  6, 7, 3,
        // Bottom
        4, 5, 1,  1, 0, 4
    };

    return m;
}
