#include "common/HalfEdgeMesh.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <queue>
#include <unordered_map>
#include <cmath>
#include <limits>

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
    if (start_he < 0 || start_he >= n_hes()) return -1;

    const int max_steps = n_hes();
    int steps = 0;

    int curr = start_he;
    bool hit_boundary = false;

    do {
        if (curr < 0 || curr >= n_hes()) break;
        if (he_to_vertex[next(curr)] == v) return curr;

        int tw = twin[curr];
        if (tw == -1) {
            hit_boundary = true;
            break;
        }
        curr = next(tw);
        if (++steps > max_steps) break;
    } while (curr != start_he);

    if (hit_boundary) {
        int prev_he = prev(start_he);
        if (prev_he >= 0 && prev_he < n_hes()) {
            curr = twin[prev_he];
            steps = 0;
            while (curr != -1 && curr != start_he) {
                if (curr < 0 || curr >= n_hes()) break;
                if (he_to_vertex[next(curr)] == v) return curr;

                int p = prev(curr);
                if (p < 0 || p >= n_hes()) break;
                curr = twin[p];
                if (++steps > max_steps) break;
            }
        }
    }
    return -1;
}

std::vector<int> HalfEdgeContainer::get_neighbors(int u) const {
    std::vector<int> neighbors;
    if (u < 0 || u >= n_vertices()) return neighbors;
    int start_he = vertex_to_he[u];
    if (start_he < 0 || start_he >= n_hes()) return neighbors;

    const int max_steps = n_hes();
    int steps = 0;

    int curr = start_he;
    bool hit_boundary = false;

    do {
        if (curr < 0 || curr >= n_hes()) break;
        int target = he_to_vertex[next(curr)];
        if (target != -1 && target != u) {
            neighbors.push_back(target);
        }

        int tw = twin[curr];
        if (tw == -1) {
            hit_boundary = true;
            break;
        }
        curr = next(tw);
        if (++steps > max_steps) break;
    } while (curr != start_he);

    if (hit_boundary) {
        int prev_he = prev(start_he);
        if (prev_he >= 0 && prev_he < n_hes()) {
            curr = twin[prev_he];
            steps = 0;
            while (curr != -1 && curr != start_he) {
                if (curr < 0 || curr >= n_hes()) break;
                int target = he_to_vertex[next(curr)];
                if (target != -1 && target != u) {
                    neighbors.push_back(target);
                }

                int p = prev(curr);
                if (p < 0 || p >= n_hes()) break;
                curr = twin[p];
                if (++steps > max_steps) break;
            }
        }
    }

    std::sort(neighbors.begin(), neighbors.end());
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    return neighbors;
}

float update_triangle(const glm::vec3& x0, const glm::vec3& x1, const glm::vec3& x2, float t1, float t2) {
    const float INF = std::numeric_limits<float>::infinity();

    float l1 = glm::distance(x1, x0);
    float l2 = glm::distance(x2, x0);

    float fallback = INF;
    if (t1 != INF) fallback = std::min(fallback, t1 + l1);
    if (t2 != INF) fallback = std::min(fallback, t2 + l2);

    if (t1 == INF || t2 == INF || l1 <= 1e-7f || l2 <= 1e-7f) {
        return fallback;
    }

    glm::vec3 v1 = x1 - x0;
    glm::vec3 v2 = x2 - x0;

    glm::mat2 E;
    E[0][0] = glm::dot(v1, v1);
    E[0][1] = glm::dot(v1, v2);
    E[1][0] = E[0][1];
    E[1][1] = glm::dot(v2, v2);

    float det = glm::determinant(E);
    if (det <= 1e-8f) {
        return fallback;
    }

    glm::mat2 Q = glm::inverse(E);

    glm::vec2 one{1.0f, 1.0f};
    glm::vec2 T{t1, t2};

    float a = glm::dot(one, Q * one);
    float b = glm::dot(one, Q * T);
    float c = glm::dot(T, Q * T) - 1.0f;

    float disc = b * b - a * c;
    if (disc >= 0.0f && a > 1e-8f) {
        float t0_candidate = (b + std::sqrt(disc)) / a;

        glm::vec2 n = Q * (glm::vec2(t0_candidate) - T);
        if (n.x >= -1e-5f && n.y >= -1e-5f && t0_candidate > std::max(t1, t2)) {
            return t0_candidate;
        }
    }

    return fallback;
}

std::vector<float> HalfEdgeContainer::compute_fast_marching_distances(int start_vertex) const {
    const int n = n_vertices();
    const float INF = std::numeric_limits<float>::infinity();
    std::vector<float> distances(n, INF);

    if (start_vertex < 0 || start_vertex >= n) {
        return distances;
    }

    std::vector<bool> vis(n, false);
    using pii = std::pair<float, int>;
    std::priority_queue<pii, std::vector<pii>, std::greater<pii>> pq;

    distances[start_vertex] = 0.0f;
    pq.push({0.0f, start_vertex});

    auto update_vertex_eikonal = [&](int u) {
        if (vis[u]) return;

        float min_dist = distances[u];
        glm::vec3 pos_u = get_vertex_pos(u);

        int start_he = vertex_to_he[u];
        if (start_he < 0 || start_he >= n_hes()) return;

        auto process_face = [&](int he) {
            if (he < 0 || he >= n_hes()) return;
            int next_he = next(he);
            int prev_he = prev(he);

            int v1 = he_to_vertex[next_he];
            int v2 = he_to_vertex[prev_he];

            if (v1 < 0 || v1 >= n || v2 < 0 || v2 >= n) return;

            float t1 = distances[v1];
            float t2 = distances[v2];

            if (t1 != INF || t2 != INF) {
                glm::vec3 pos_v1 = get_vertex_pos(v1);
                glm::vec3 pos_v2 = get_vertex_pos(v2);

                float candidate_t = update_triangle(pos_u, pos_v1, pos_v2, t1, t2);
                min_dist = std::min(min_dist, candidate_t);
            }
        };

        const int max_steps = n_hes();
        int steps = 0;

        int curr_he = start_he;
        bool hit_boundary = false;
        do {
            if (curr_he < 0 || curr_he >= n_hes()) break;
            process_face(curr_he);

            int tw = twin[curr_he];
            if (tw == -1) {
                hit_boundary = true;
                break;
            }
            curr_he = next(tw);
            if (++steps > max_steps) break;
        } while (curr_he != start_he);

        if (hit_boundary) {
            int prev_he = prev(start_he);
            if (prev_he >= 0 && prev_he < n_hes()) {
                curr_he = twin[prev_he];
                steps = 0;
                while (curr_he != -1 && curr_he != start_he) {
                    if (curr_he < 0 || curr_he >= n_hes()) break;
                    process_face(curr_he);

                    int p = prev(curr_he);
                    if (p < 0 || p >= n_hes()) break;
                    curr_he = twin[p];
                    if (++steps > max_steps) break;
                }
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

        glm::vec3 pos_u = get_vertex_pos(u);
        for (int v : get_neighbors(u)) {
            if (!vis[v]) {
                float edge_len = glm::distance(pos_u, get_vertex_pos(v));
                if (distances[u] + edge_len < distances[v]) {
                    distances[v] = distances[u] + edge_len;
                    pq.push({distances[v], v});
                }
                update_vertex_eikonal(v);
            }
        }
    }
    return distances;
}

HalfEdgeContainer GeometryUtils::buildHalfEdge(const MeshData &m) {
    const size_t num_indices = m.indices.size();
    const size_t num_vertices = m.vertices.size();

    if (num_indices % 3 != 0 || num_vertices == 0) return {};

    std::vector<int> he_to_vertex(num_indices);
    std::vector<int> vertex_to_he(num_vertices, -1);
    std::vector<int> twin(num_indices, -1);

    std::unordered_map<uint64_t, int> edge_to_he;
    edge_to_he.reserve(num_indices);

    auto make_key = [](int u, int v) -> uint64_t {
        return (static_cast<uint64_t>(u) << 32) | static_cast<uint32_t>(v);
    };

    for (size_t i = 0; i < num_indices; i += 3) {
        int v0 = m.indices[i];
        int v1 = m.indices[i + 1];
        int v2 = m.indices[i + 2];

        if (v0 >= (int)num_vertices || v1 >= (int)num_vertices || v2 >= (int)num_vertices ||
            v0 < 0 || v1 < 0 || v2 < 0) return {};

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

    return HalfEdgeContainer{m.vertices, vertex_to_he, he_to_vertex, twin};
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
                    m.uvs.emplace_back(0, 0);
                }
                m.indices.push_back(v_remap[old_v]);
            }
        }
    }
    return m;
}