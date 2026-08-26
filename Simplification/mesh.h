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
#include "linalg.h"

const float PI = 3.14159265358979323846;
using namespace linalg::aliases;

int next(int he) {
    int relative_pos = he % 3;
    int new_pos = (relative_pos + 1) % 3;
    return he - relative_pos + new_pos;
}
int prev(int he) {
    int relative_pos = he % 3;
    int new_pos = (relative_pos + 2) % 3;
    return he - relative_pos + new_pos;
}

struct HalfEdgeContainer {
    std::vector<float> vertices;  // 3 * nvertices
    std::vector<int> vertex_to_he; // nvertices
    std::vector<int> he_to_vertex; // 3 * faces = halfedges
    std::vector<int> twin; // 3 * face  = halfedges

   float3 get_vertex(int he) {
        int vertex_pos = he_to_vertex[he];
        return float3{vertices[3*vertex_pos], vertices[3*vertex_pos+1], vertices[3*vertex_pos+2]};
   }

    void simplify(int edges_to_remove) {
       std::vector<float4x4> Q(vertices.size() / 3);
       for (int i = 0; i < n_vertices(); i++) {
           float4x4 sumQ;
           int init_he = vertex_to_he[i];
           int current_he = init_he;
           do {
               int he0 = current_he;
               int he1 = next(he0);
               int he2 = next(he1);
               auto v0 = get_vertex(he0);
               auto v1 = get_vertex(he1);
               auto v2 = get_vertex(he2);

               // plane
               auto normal = cross(v1 - v0, v2 - v0); // A B C
               auto d = -dot(normal, v0); // D
               linalg::mat<float, 1, 4> p{normal.x, normal.y, normal.z, d};
               auto current_Q = mul(linalg::transpose(p), p);
               sumQ += current_Q;
               current_he = twin[prev(current_he)];
           } while (current_he != -1 && current_he != init_he);

           Q[i] = sumQ;
       }

       struct queueData {
           int u, v;
           float err;
           float4 vp;
       };

       std::map<std::pair<int, int>, bool> vis;
       std::map<std::pair<float, int>, queueData> queue;
       std::map<std::pair<int, int>, std::pair<float, int>> edge_to_key;
       int id = 0;
        for (int he = 0; he < he_to_vertex.size(); he++ ) {
            int u = he_to_vertex[he];
            int v = next(he);

            if (vis[{v, u}]) {
                continue;
            }
            vis[{u, v}] = true;

            auto Qp = Q[u] + Q[v];
            Qp[4][0] = 0;
            Qp[4][1] = 0;
            Qp[4][2] = 0;
            Qp[4][3] = 1;

            auto vp = mul(inverse(Qp), linalg::mat<float, 4, 1>{float4{0, 0, 0, 1}});
            auto err = mul(mul(transpose(vp),Q[u]+Q[v]), vp).x.x;

            queue[{err, id}]  = queueData{
                u, v, err , vp[0],
            };
            edge_to_key[{u, v}] = {err, id};
            id++;
        }

       for (int i = 0; i < edges_to_remove; i++) {
           auto it = queue.begin();
           queue.erase(it);

       }
    }

    int n_vertices() {
        return vertices.size() / 3;
    }

    int n_hes() {
       return he_to_vertex.size() ;
    }
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
