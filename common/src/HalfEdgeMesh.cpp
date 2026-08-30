#include "common/HalfEdgeMesh.h"
#include <algorithm>
#include <iostream>
#include <map>

using namespace linalg::aliases;

int HalfEdgeContainer::n_vertices() const {
    return static_cast<int>(vertices.size() / 3);
}

int HalfEdgeContainer::n_hes() const {
    return static_cast<int>(he_to_vertex.size());
}

float3 HalfEdgeContainer::get_vertex_pos(int v_idx) const {
    return float3{vertices[3 * v_idx], vertices[3 * v_idx + 1], vertices[3 * v_idx + 2]};
}

float3 HalfEdgeContainer::get_vertex(int he) const {
    int vertex_pos = he_to_vertex[he];
    return get_vertex_pos(vertex_pos);
}

int HalfEdgeContainer::get_he(int u, int v) const {
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

std::vector<int> HalfEdgeContainer::get_neighbors(int u) const {
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



Mesh HalfEdgeContainer::to_mesh() const {
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

HalfEdgeContainer NewHalfEdgeContainer(const Mesh& m) {
    std::vector<int> he_to_vertex(m.indices.size());
    std::vector<int> vertex_to_he(m.vertices.size() / 3, -1);
    std::vector<int> twin(m.indices.size(), -1);

    std::map<std::pair<int, int>, int> edge_to_he;

    for (size_t i = 0; i < m.indices.size(); i += 3) {
        int v0 = m.indices[i];
        int v1 = m.indices[i + 1];
        int v2 = m.indices[i + 2];

        he_to_vertex[i] = v0;
        vertex_to_he[v0] = static_cast<int>(i);
        edge_to_he[{v0, v1}] = static_cast<int>(i);

        he_to_vertex[i + 1] = v1;
        vertex_to_he[v1] = static_cast<int>(i + 1);
        edge_to_he[{v1, v2}] = static_cast<int>(i + 1);

        he_to_vertex[i + 2] = v2;
        vertex_to_he[v2] = static_cast<int>(i + 2);
        edge_to_he[{v2, v0}] = static_cast<int>(i + 2);
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
