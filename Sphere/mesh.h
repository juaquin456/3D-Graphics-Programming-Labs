//
// Created by juaquin on 8/18/26.
//

#ifndef LEARN_OPENGL_MESH_H
#define LEARN_OPENGL_MESH_H
#include <algorithm>
#include <cmath>
#include <map>
#include <ranges>
#include <vector>
#include <map>

const float PI = 3.14159265358979323846;


struct HalfEdgeContainer {
    std::vector<float> vertices;  // 3 * nvertices
    std::vector<int> vertex_to_he; // nvertices
    std::vector<int> he_to_vertex; // 3 * faces = halfedges
    std::vector<int> twin; // 3 * face  = halfedges
};

struct Mesh {
    std::vector<float> vertices; // 3 * nvertices
    std::vector<int> indices; // 3 * faces
};

HalfEdgeContainer NewHalfEdgeContainer(const Mesh& m ) {
    std::vector<int> he_to_vertex(m.indices.size());
    std::vector<int> vertex_to_he(m.vertices.size()/3);
    // std::vector<int> face_to_half_edge(indices.size() / 3);
    std::vector<int> twin(m.indices.size(), -1);

    std::map<std::pair<int, int>, int> edge_to_he;

    for (int i = 0; i < m.indices.size(); i+=3) {
        // face_to_half_edge[i/3] = i;

        he_to_vertex[i] = m.indices[i];
        vertex_to_he[m.indices[i]] = i;
        edge_to_he[{m.indices[i], m.indices[i+1]}] = i;

        he_to_vertex[i+1] = m.indices[i+1];
        vertex_to_he[m.indices[i+1]] = i+1;
        edge_to_he[{m.indices[i+1], m.indices[i+2]}] = i+1;

        he_to_vertex[i+2] = m.indices[i+2];
        vertex_to_he[m.indices[i+2]] = i+2;
        edge_to_he[{m.indices[i+2], m.indices[i]}] = i+2;
    }

    std::vector<std::pair<int, int>> edges;

    for (const auto &edge: edge_to_he | std::views::keys) {
        edges.push_back(edge);
    }
    for (const auto& edge: edges) {
        auto it = edge_to_he.find(edge);
        if (it == edge_to_he.end()) {
            std::cout << "ERROR" << std::endl;
            continue;
        }
        int he = it->second;

        int u = edge.first;
        int v = edge.second;
        it = edge_to_he.find({v, u});
        if (it != edge_to_he.end()) {
            twin[he] = it->second;
        } else {
            std::cout << "not found twin of " << u << " " << v << std::endl;
        }
    }


    return HalfEdgeContainer{
        m.vertices, vertex_to_he, he_to_vertex, twin,
    };
}
Mesh NewSphere(float radius, int slices, int stacks) {
    Mesh m;
    for (int i = 0; i < slices; i++) {
        float a = PI * i / slices;
        for (int j = 0; j < stacks; j++) {
            float b = 2. * PI * j / stacks;
            float x = radius * sin(a) * cos(b);
            float y = radius * sin(a) * sin(b);
            float z = radius * cos(a);
            m.vertices.push_back(x);
            m.vertices.push_back(y);
            m.vertices.push_back(z);
        }
    }

    int total_vertices = m.vertices.size()/3;

    for (int i = 0; i < slices; i++) {
        for (int j = 0; j < stacks; j++) {
            m.indices.push_back((i*stacks + j)%total_vertices);
            m.indices.push_back((i*stacks + j+1)%total_vertices);
            m.indices.push_back(((i+1)*stacks + j)%total_vertices);

            m.indices.push_back(((i+1)*stacks + j)%total_vertices);
            m.indices.push_back((i*stacks + j + 1)%total_vertices);
            m.indices.push_back(((i+1)*stacks + j + 1)%total_vertices);
        }
    }

    return m;
}

#endif //LEARN_OPENGL_MESH_H
