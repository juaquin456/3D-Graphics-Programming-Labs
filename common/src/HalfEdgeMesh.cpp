#include "common/HalfEdgeMesh.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <queue>
#include <unordered_map>
#include <cmath>

int HalfEdgeContainer::n_vertices() const {
    return static_cast<int>(vertices.size());
}

int HalfEdgeContainer::n_hes() const {
    return static_cast<int>(he_to_vertex.size());
}

glm::vec3 HalfEdgeContainer::get_vertex_pos(int v_idx) const {
    return vertices[v_idx];
}

glm::vec3 HalfEdgeContainer::get_vertex(int he) const {
    int vertex_pos = he_to_vertex[he];
    return get_vertex_pos(vertex_pos);
}

int HalfEdgeContainer::get_he(int u, int v) const {
    if (u < 0 || u >= n_vertices() || v < 0 || v >= n_vertices()) return -1;
    int start_he = vertex_to_he[u];
    if (start_he == -1) return -1;

    int curr = start_he;
    do {
        if (he_to_vertex[next(curr)] == v) return curr;
        int tw = twin[curr];
        if (tw == -1) break;
        curr = next(tw);
    } while (curr != start_he && curr != -1);

    if (twin[curr] == -1) {
        int prev_he = prev(start_he);
        curr = (prev_he != -1) ? twin[prev_he] : -1;
        while (curr != -1 && curr != start_he) {
            if (he_to_vertex[next(curr)] == v) return curr;
            int p = prev(curr);
            curr = (p != -1) ? twin[p] : -1;
        }
    }
    return -1;
}

std::vector<int> HalfEdgeContainer::get_neighbors(int u) const {
    std::vector<int> neighbors;
    if (u < 0 || u >= n_vertices()) return neighbors;
    int start_he = vertex_to_he[u];
    if (start_he == -1) return neighbors;

    int curr = start_he;
    do {
        int target = he_to_vertex[next(curr)];
        if (target != -1 && target != u) neighbors.push_back(target);
        int tw = twin[curr];
        if (tw == -1) break;
        curr = next(tw);
    } while (curr != start_he && curr != -1);

    if (twin[curr] == -1) {
        int prev_he = prev(start_he);
        curr = (prev_he != -1) ? twin[prev_he] : -1;
        while (curr != -1 && curr != start_he) {
            int target = he_to_vertex[next(curr)];
            if (target != -1 && target != u) neighbors.push_back(target);
            int p = prev(curr);
            curr = (p != -1) ? twin[p] : -1;
        }
    }

    std::sort(neighbors.begin(), neighbors.end());
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    return neighbors;
}

float update_triangle(const glm::vec3& x0, const glm::vec3& x1, const glm::vec3& x2, float t1, float t2) {
    glm::vec3 v1 = x1 - x0;
    glm::vec3 v2 = x2 - x0;

    glm::mat2x3 X(v1, v2);

    auto E = glm::transpose(X) * X;

    float det = glm::determinant(E);
    if (det <= 1e-8f) {
        return std::min(t1 + std::sqrt(E[0][0]), t2 + std::sqrt(E[1][1]));
    }

    auto Q = glm::inverse(E);

    float q11 = Q[0][0], q12 = Q[0][1];
    float q21 = Q[1][0], q22 = Q[1][1];

    glm::vec2 one{1.0f, 1.0f};
    glm::vec2 T{t1, t2};

    float a = dot(one, Q * one);
    float b = dot(one, Q *  T);
    float c = dot(T, Q * T) - 1.0f;

    float disc = b * b - a * c;
    float t0 = std::numeric_limits<float>::infinity();

    if (disc >= 0.0f) {
        float t0_candidate = (b + std::sqrt(disc)) / a;

        float cond1 = q11 * (t1 - t0_candidate) + q12 * (t2 - t0_candidate);
        float cond2 = q21 * (t1 - t0_candidate) + q22 * (t2 - t0_candidate);

        if (cond1 < 0.0f && cond2 < 0.0f && t0_candidate > std::max(t1, t2)) {
            t0 = t0_candidate;
        }
    }

    if (t0 == std::numeric_limits<float>::infinity()) {
        t0 = std::min(t1 + std::sqrt(E[0][0]), t2 + std::sqrt(E[1][1]));
    }

    return t0;
}

std::vector<float> HalfEdgeContainer::compute_fast_marching_distances(int start_vertex) const {
    const int n = this->n_vertices();
    const float INF = std::numeric_limits<float>::infinity();

    int start_pos = start_vertex;
    if (!orig_to_pos.empty()) {
        if (start_vertex >= 0 && start_vertex < static_cast<int>(orig_to_pos.size())) {
            start_pos = orig_to_pos[start_vertex];
        }
    }

    std::vector<float> distances(n, INF);

    if (start_pos < 0 || start_pos >= n) {
        if (!orig_to_pos.empty()) return std::vector<float>(orig_to_pos.size(), INF);
        return distances;
    }

    std::vector<bool> vis(n, false);
    using pii = std::pair<float, int>;
    std::priority_queue<pii, std::vector<pii>, std::greater<pii>> pq;

    distances[start_pos] = 0.0f;
    pq.push({0.0f, start_pos});

    auto update_vertex_eikonal = [&](int u) {
        if (vis[u]) return;

        float min_dist = distances[u];
        glm::vec3 pos_u = this->get_vertex_pos(u);

        int start_he = this->vertex_to_he[u];
        if (start_he == -1) return;

        auto process_face = [&](int he) {
            if (he == -1) return;
            int next_he = next(he);
            int prev_he = prev(he);

            int v1 = this->he_to_vertex[next_he];
            int v2 = this->he_to_vertex[prev_he];

            float t1 = distances[v1];
            float t2 = distances[v2];

            if (t1 != INF || t2 != INF) {
                glm::vec3 pos_v1 = this->get_vertex_pos(v1);
                glm::vec3 pos_v2 = this->get_vertex_pos(v2);

                float candidate_t = update_triangle(pos_u, pos_v1, pos_v2, t1, t2);
                min_dist = std::min(min_dist, candidate_t);
            }
        };

        int curr_he = start_he;
        do {
            process_face(curr_he);
            int tw = this->twin[curr_he];
            if (tw == -1) break;
            curr_he = next(tw);
        } while (curr_he != start_he && curr_he != -1);

        if (curr_he != -1 && this->twin[curr_he] == -1) {
            int p_he = prev(start_he);
            curr_he = (p_he != -1) ? this->twin[p_he] : -1;

            while (curr_he != -1 && curr_he != start_he) {
                process_face(curr_he);
                int p = prev(curr_he);
                curr_he = (p != -1) ? this->twin[p] : -1;
            }
        }

        if (min_dist < distances[u]) {
            distances[u] = min_dist;
            pq.push({min_dist, u});
        }
    };

    while (!pq.empty()) {
        auto [current_dist, u] = pq.top();
        pq.pop();

        if (vis[u]) continue;
        vis[u] = true;

        for (int v : this->get_neighbors(u)) {
            if (!vis[v]) {
                update_vertex_eikonal(v);
            }
        }
    }

    if (!orig_to_pos.empty()) {
        std::vector<float> orig_distances(orig_to_pos.size());
        for (size_t i = 0; i < orig_to_pos.size(); ++i) {
            orig_distances[i] = distances[orig_to_pos[i]];
        }
        return orig_distances;
    }

    return distances;
}

struct PosKey {
    int x, y, z;
    bool operator==(const PosKey& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct PosKeyHash {
    std::size_t operator()(const PosKey& k) const {
        return ((std::hash<int>()(k.x) ^ (std::hash<int>()(k.y) << 1)) >> 1) ^ (std::hash<int>()(k.z) << 1);
    }
};

static PosKey makePosKey(const glm::vec3& v, float eps = 1e-4f) {
    return PosKey{
        static_cast<int>(std::floor(v.x / eps)),
        static_cast<int>(std::floor(v.y / eps)),
        static_cast<int>(std::floor(v.z / eps))
    };
}

HalfEdgeContainer GeometryUtils::buildHalfEdge(const MeshData &m) {
    const size_t num_indices = m.indices.size();
    const size_t num_orig_vertices = m.vertices.size();

    if (num_indices % 3 != 0 || num_orig_vertices == 0) return {};

    std::vector<glm::vec3> unique_positions;
    std::vector<int> orig_to_pos(num_orig_vertices, -1);
    std::unordered_map<PosKey, int, PosKeyHash> pos_map;
    pos_map.reserve(num_orig_vertices);

    for (size_t i = 0; i < num_orig_vertices; ++i) {
        PosKey key = makePosKey(m.vertices[i]);
        auto it = pos_map.find(key);
        if (it != pos_map.end()) {
            orig_to_pos[i] = it->second;
        } else {
            int pos_id = static_cast<int>(unique_positions.size());
            unique_positions.push_back(m.vertices[i]);
            pos_map[key] = pos_id;
            orig_to_pos[i] = pos_id;
        }
    }

    const size_t num_unique_verts = unique_positions.size();

    std::vector<int> he_to_vertex(num_indices);
    std::vector<int> vertex_to_he(num_unique_verts, -1);
    std::vector<int> twin(num_indices, -1);

    std::unordered_map<uint64_t, int> edge_to_he;
    edge_to_he.reserve(num_indices);

    auto make_key = [](int u, int v) -> uint64_t {
        return (static_cast<uint64_t>(u) << 32) | static_cast<uint32_t>(v);
    };

    for (size_t i = 0; i < num_indices; i += 3) {
        int o0 = m.indices[i];
        int o1 = m.indices[i + 1];
        int o2 = m.indices[i + 2];

        if (o0 >= (int)num_orig_vertices || o1 >= (int)num_orig_vertices || o2 >= (int)num_orig_vertices ||
            o0 < 0 || o1 < 0 || o2 < 0) return {};

        int v0 = orig_to_pos[o0];
        int v1 = orig_to_pos[o1];
        int v2 = orig_to_pos[o2];

        he_to_vertex[i]     = v0;
        he_to_vertex[i + 1] = v1;
        he_to_vertex[i + 2] = v2;

        vertex_to_he[v0] = static_cast<int>(i);
        vertex_to_he[v1] = static_cast<int>(i + 1);
        vertex_to_he[v2] = static_cast<int>(i + 2);

        edge_to_he[make_key(v0, v1)] = static_cast<int>(i);
        edge_to_he[make_key(v1, v2)] = static_cast<int>(i + 1);
        edge_to_he[make_key(v2, v0)] = static_cast<int>(i + 2);
    }

    for (size_t i = 0; i < num_indices; ++i) {
        int u = he_to_vertex[i];
        int next_he = (i - (i % 3)) + ((i % 3 + 1) % 3);
        int v = he_to_vertex[next_he];

        auto it = edge_to_he.find(make_key(v, u));
        if (it != edge_to_he.end()) {
            twin[i] = it->second;
        }
    }

    return HalfEdgeContainer{unique_positions, vertex_to_he, he_to_vertex, twin, orig_to_pos};
}

MeshData GeometryUtils::buildMeshData(const HalfEdgeContainer &he) {
    MeshData m;
    std::vector<int> v_remap(he.n_vertices(), -1);
    int new_v_count = 0;

    for (int f = 0; f < he.n_hes() / 3; ++f) {
        int h0 = 3 * f;
        int h1 = h0 + 1;
        int h2 = h0 + 2;

        int u0 = he.he_to_vertex[h0];
        int u1 = he.he_to_vertex[h1];
        int u2 = he.he_to_vertex[h2];

        if (u0 != -1 && u1 != -1 && u2 != -1 && u0 != u1 && u1 != u2 && u2 != u0) {
            int idxs[3] = {u0, u1, u2};
            for (int k = 0; k < 3; ++k) {
                int old_v = idxs[k];
                if (v_remap[old_v] == -1) {
                    v_remap[old_v] = new_v_count++;
                    m.vertices.emplace_back(he.vertices[old_v]);
                }
                m.indices.push_back(v_remap[old_v]);
            }
        }
    }
    return m;
}
