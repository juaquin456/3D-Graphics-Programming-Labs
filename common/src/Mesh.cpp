#include "common/Mesh.h"
#include "happly.h"
#include <array>
#include <cmath>

static constexpr float PI = 3.14159265358979323846f;

Mesh::Mesh(const std::string &filename) {
    happly::PLYData ply_data(filename);
    std::vector<std::array<double, 3>> v_pos = ply_data.getVertexPositions();
    std::vector<std::vector<size_t>> f_ind = ply_data.getFaceIndices<size_t>();


    for (auto & v_po : v_pos) {
        vertices.push_back(v_po[0]);
        vertices.push_back(v_po[1]);
        vertices.push_back(v_po[2]);
    }

    for (auto & i : f_ind) {
        for (const auto v: i) {
            indices.push_back(v);
        }
    }
}


void Mesh::save(const std::string& filename) const {
    happly::PLYData ply_out;

    std::vector<std::array<double, 3>> out_vertices(vertices.size() / 3);
    for (size_t i = 0; i < vertices.size(); i += 3) {
        out_vertices[i / 3] = {vertices[i], vertices[i + 1], vertices[i + 2]};
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

Mesh NewSphere(float radius, int slices, int stacks) {
    Mesh m;

    m.vertices.push_back(0.0f);
    m.vertices.push_back(0.0f);
    m.vertices.push_back(radius);

    for (int i = 1; i < stacks; i++) {
        float a = PI * i / stacks;
        float sin_a = std::sin(a);
        float cos_a = std::cos(a);

        for (int j = 0; j < slices; j++) {
            float b = 2.0f * PI * j / slices;
            float x = radius * sin_a * std::cos(b);
            float y = radius * sin_a * std::sin(b);
            float z = radius * cos_a;

            m.vertices.push_back(x);
            m.vertices.push_back(y);
            m.vertices.push_back(z);
        }
    }

    m.vertices.push_back(0.0f);
    m.vertices.push_back(0.0f);
    m.vertices.push_back(-radius);

    int south_pole_idx = static_cast<int>(m.vertices.size() / 3) - 1;

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
