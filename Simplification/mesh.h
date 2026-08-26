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
#include "queue.h"
#include "happly.h"

const float PI = 3.14159265358979323846;
using namespace linalg::aliases;

inline int next(int he) {
    int relative_pos = he % 3;
    int new_pos = (relative_pos + 1) % 3;
    return he - relative_pos + new_pos;
}

inline int prev(int he) {
    int relative_pos = he % 3;
    int new_pos = (relative_pos + 2) % 3;
    return he - relative_pos + new_pos;
}

struct Mesh {
    std::vector<float> vertices; // 3 * nvertices
    std::vector<int> indices;    // 3 * faces

    void save(std::string filename) {
        happly::PLYData ply_out;

        std::vector<std::array<double, 3>> out_vertices(vertices.size()/3);
        for (int i = 0; i < vertices.size(); i+=3) {
            out_vertices[i/3] = {vertices[i], vertices[i+1], vertices[i+2]};
        }

        std::vector<std::vector<size_t>> out_face_indices(indices.size()/3);
        for (int i = 0; i < indices.size(); i+=3) {
            out_face_indices[i/3] = {static_cast<size_t>(indices[i]), static_cast<size_t>(indices[i + 1]), static_cast<size_t>(indices[i + 2])};
        }

        ply_out.addVertexPositions(out_vertices);
        ply_out.addFaceIndices(out_face_indices);

        ply_out.write(filename, happly::DataFormat::ASCII);
    }
};

struct HalfEdgeContainer {
    std::vector<float> vertices;   // 3 * nvertices
    std::vector<int> vertex_to_he; // nvertices
    std::vector<int> he_to_vertex; // 3 * faces = halfedges
    std::vector<int> twin;         // 3 * faces = halfedges

    int n_vertices() const { return vertices.size() / 3; }
    int n_hes() const { return he_to_vertex.size(); }

    float3 get_vertex_pos(int v_idx) const {
        return float3{vertices[3 * v_idx], vertices[3 * v_idx + 1], vertices[3 * v_idx + 2]};
    }

    float3 get_vertex(int he) const {
        int vertex_pos = he_to_vertex[he];
        return get_vertex_pos(vertex_pos);
    }

    int get_he(int u, int v) const {
        int start_he = vertex_to_he[u];
        if (start_he == -1) return -1;

        int curr = start_he;
        do {
            if (he_to_vertex[curr] == u && he_to_vertex[next(curr)] == v) {
                return curr;
            }
            int tw = twin[curr];
            if (tw == -1) break;
            curr = next(tw);
        } while (curr != start_he && curr != -1);

        if (twin[curr] == -1) {
            curr = twin[prev(start_he)];
            while (curr != -1 && curr != start_he) {
                if (he_to_vertex[curr] == u && he_to_vertex[next(curr)] == v) {
                    return curr;
                }
                curr = twin[prev(curr)];
            }
        }
        return -1;
    }

    std::vector<int> get_neighbors(int u) const {
        std::vector<int> neighbors;
        int start_he = vertex_to_he[u];
        if (start_he == -1) return neighbors;

        int curr = start_he;
        do {
            if (he_to_vertex[curr] == u) {
                int target = he_to_vertex[next(curr)];
                if (target != -1 && target != u) {
                    neighbors.push_back(target);
                }
            }
            int tw = twin[curr];
            if (tw == -1) break;
            curr = next(tw);
        } while (curr != start_he && curr != -1);

        if (twin[curr] == -1) {
            curr = twin[prev(start_he)];
            while (curr != -1 && curr != start_he) {
                if (he_to_vertex[curr] == u) {
                    int target = he_to_vertex[next(curr)];
                    if (target != -1 && target != u) {
                        neighbors.push_back(target);
                    }
                }
                curr = twin[prev(curr)];
            }
        }

        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
        return neighbors;
    }

    void compute_target(int u, int v, const std::vector<float4x4>& Q, float4& out_vp, float& out_err) const {
        float4x4 Qp = Q[u] + Q[v];
        float4x4 Qp_inv = Qp;

        Qp_inv[0].w = 0.0f;
        Qp_inv[1].w = 0.0f;
        Qp_inv[2].w = 0.0f;
        Qp_inv[3].w = 1.0f;

        float det = linalg::determinant(Qp_inv);
        if (std::abs(det) > 1e-6f) {
            auto inv = linalg::inverse(Qp_inv);
            out_vp = mul(inv, float4{0.0f, 0.0f, 0.0f, 1.0f});
            out_vp.w = 1.0f;
        } else {
            std::cerr << "no inverse: " << det << std::endl;
            float3 p_u = get_vertex_pos(u);
            float3 p_v = get_vertex_pos(v);
            float3 p_mid = (p_u + p_v) * 0.5f;

            float4 candidates[3] = { float4{p_u, 1.0f}, float4{p_v, 1.0f}, float4{p_mid, 1.0f} };
            float min_e = 1e30f;
            float4 best_vp = candidates[0];

            for (int k = 0; k < 3; ++k) {
                float e = dot(candidates[k], mul(Qp, candidates[k]));
                if (e < min_e) {
                    min_e = e;
                    best_vp = candidates[k];
                }
            }
            out_vp = best_vp;
        }

        out_err = dot(out_vp, mul(Qp, out_vp));
        if (out_err < 0.0f) out_err = 0.0f;
    }


    void simplify(int edges_to_remove) {
        std::vector<float4x4> Q(n_vertices());
        for (int i = 0; i < n_vertices(); i++) {
            float4x4 sumQ;
            int init_he = vertex_to_he[i];
            if (init_he == -1) continue;
            int current_he = init_he;
            do {
                int he0 = current_he;
                int he1 = next(he0);
                int he2 = next(he1);
                auto v0 = get_vertex(he0);
                auto v1 = get_vertex(he1);
                auto v2 = get_vertex(he2);

                auto normal = normalize(cross(v1 - v0, v2 - v0));
                auto d = -dot(normal, v0);
                linalg::mat<float, 1, 4> p{normal.x, normal.y, normal.z, d};
                sumQ += mul(linalg::transpose(p), p);

                current_he = twin[prev(current_he)];
            } while (current_he != -1 && current_he != init_he);

            Q[i] = sumQ;
        }

        QueueSystem q;
        for (int he = 0; he < n_hes(); he++) {
            int u = he_to_vertex[he];
            int v = he_to_vertex[next(he)];

            if (u != -1 && v != -1 && u < v) {
                float4 vp;
                float err;
                compute_target(u, v, Q, vp, err);
                q.push_or_update(u, v, err, vp);
            }
        }

        int removed = 0;
        while (!q.empty() && removed < edges_to_remove) {
            queueData top;
            if (!q.pop(top)) break;

            int u = top.u;
            int v = top.v;

            if (vertex_to_he[u] == -1 || vertex_to_he[v] == -1) continue;

            vertices[3 * u]     = top.vp.x;
            vertices[3 * u + 1] = top.vp.y;
            vertices[3 * u + 2] = top.vp.z;

            Q[u] = Q[u] + Q[v];

            int he_uv = get_he(u, v);
            int he_vu = get_he(v, u);

            auto collapse_face = [&](int he) {
                if (he == -1) return;
                int h_next = next(he);
                int h_prev = prev(he);

                int t_next = twin[h_next];
                int t_prev = twin[h_prev];

                if (t_next != -1) twin[t_next] = t_prev;
                if (t_prev != -1) twin[t_prev] = t_next;

                he_to_vertex[he]     = -1;
                he_to_vertex[h_next] = -1;
                he_to_vertex[h_prev] = -1;
            };

            collapse_face(he_uv);
            collapse_face(he_vu);

            vertex_to_he[u] = -1;
            for (int h = 0; h < n_hes(); ++h) {
                if (he_to_vertex[h] == v) {
                    he_to_vertex[h] = u;
                }
                if (he_to_vertex[h] == u) {
                    vertex_to_he[u] = h;
                }
            }
            vertex_to_he[v] = -1;

            q.erase_edge(u, v);
            std::vector<int> neighbors = get_neighbors(u);

            for (int n : neighbors) {
                if (n == u) continue;

                q.erase_edge(v, n);

                float4 new_vp;
                float new_err;
                compute_target(u, n, Q, new_vp, new_err);

                q.push_or_update(u, n, new_err, new_vp);
            }

            removed++;
        }
    }

    Mesh to_mesh() const {
        Mesh m;
        std::vector<int> v_remap(n_vertices(), -1);
        int new_v_count = 0;

        for (int f = 0; f < n_hes() / 3; ++f) {
            int h0 = 3 * f;
            int h1 = h0 + 1;
            int h2 = h0 + 2;

            int u0 = he_to_vertex[h0];
            int u1 = he_to_vertex[h1];
            int u2 = he_to_vertex[h2];

            if (u0 != -1 && u1 != -1 && u2 != -1 && u0 != u1 && u1 != u2 && u2 != u0) {
                int idxs[3] = {u0, u1, u2};
                for (int k = 0; k < 3; ++k) {
                    int old_v = idxs[k];
                    if (v_remap[old_v] == -1) {
                        v_remap[old_v] = new_v_count++;
                        m.vertices.push_back(vertices[3 * old_v]);
                        m.vertices.push_back(vertices[3 * old_v + 1]);
                        m.vertices.push_back(vertices[3 * old_v + 2]);
                    }
                    m.indices.push_back(v_remap[old_v]);
                }
            }
        }
        return m;
    }
};

HalfEdgeContainer NewHalfEdgeContainer(const Mesh& m) {
    std::vector<int> he_to_vertex(m.indices.size());
    std::vector<int> vertex_to_he(m.vertices.size() / 3, -1);
    std::vector<int> twin(m.indices.size(), -1);

    std::map<std::pair<int, int>, int> edge_to_he;

    for (size_t i = 0; i < m.indices.size(); i += 3) {
        he_to_vertex[i] = m.indices[i];
        vertex_to_he[m.indices[i]] = i;
        edge_to_he[{m.indices[i], m.indices[i + 1]}] = i;

        he_to_vertex[i + 1] = m.indices[i + 1];
        vertex_to_he[m.indices[i + 1]] = i + 1;
        edge_to_he[{m.indices[i + 1], m.indices[i + 2]}] = i + 1;

        he_to_vertex[i + 2] = m.indices[i + 2];
        vertex_to_he[m.indices[i + 2]] = i + 2;
        edge_to_he[{m.indices[i + 2], m.indices[i]}] = i + 2;
    }

    for (const auto& [edge, he] : edge_to_he) {
        int u = edge.first;
        int v = edge.second;
        auto it = edge_to_he.find({v, u});
        if (it != edge_to_he.end()) {
            twin[he] = it->second;
        }
    }

    return HalfEdgeContainer{
        m.vertices, vertex_to_he, he_to_vertex, twin
    };
}

Mesh NewSphere(float radius, int slices, int stacks) {
    Mesh m;

    // 1. Vértice del Polo Norte (índice 0)
    m.vertices.push_back(0.0f);
    m.vertices.push_back(0.0f);
    m.vertices.push_back(radius);

    for (int i = 1; i < stacks; i++) {
        float a = PI * i / stacks;
        float sin_a = sin(a);
        float cos_a = cos(a);

        for (int j = 0; j < slices; j++) {
            float b = 2.0f * PI * j / slices;
            float x = radius * sin_a * cos(b);
            float y = radius * sin_a * sin(b);
            float z = radius * cos_a;

            m.vertices.push_back(x);
            m.vertices.push_back(y);
            m.vertices.push_back(z);
        }
    }

    m.vertices.push_back(0.0f);
    m.vertices.push_back(0.0f);
    m.vertices.push_back(-radius);

    int south_pole_idx = (m.vertices.size() / 3) - 1;


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

            // Segundo triángulo
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

#endif //LEARN_OPENGL_MESH_H
