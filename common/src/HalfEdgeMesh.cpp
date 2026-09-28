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

void HalfEdgeContainer::get_neighbors(
    int u,
    std::vector<int>& neighbors
) const {
    neighbors.clear();

    if (u < 0 || u >= n_vertices()) return;

    int start_he = vertex_to_he[u];
    if (start_he < 0 || start_he >= n_hes()) return;

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
    const int n_he = n_hes();
    const float INF = std::numeric_limits<float>::infinity();
    std::vector<float> distances(n, INF);

    if (start_vertex < 0 || start_vertex >= n) {
        return distances;
    }

    std::vector<uint8_t> vis(n, 0);

    using pii = std::pair<float, int>;
    std::priority_queue<pii, std::vector<pii>, std::greater<pii>> pq;

    distances[start_vertex] = 0.0f;
    pq.push({0.0f, start_vertex});


    while (!pq.empty()) {
        auto [current_dist, u] = pq.top();
        pq.pop();

        if (vis[u]) continue;
        vis[u] = 1;

        int start_he = vertex_to_he[u];
        if (start_he < 0 || start_he >= n_he) continue;

        int curr_he = start_he;
        int steps = 0;
        bool hit_boundary = false;

        do {
            if (curr_he < 0 || curr_he >= n_he) break;
            int v = he_to_vertex[next(curr_he)];

            if (v >= 0 && v < n && !vis[v]) {
                glm::vec3 pos_u = vertices[u];
                glm::vec3 pos_v = vertices[v];

                float edge_len = glm::distance(pos_u, pos_v);
                float new_d = distances[u] + edge_len;

                int next_h = next(curr_he);
                int prev_h = prev(curr_he);
                int w = he_to_vertex[prev_h];

                if (w >= 0 && w < n) {
                    float tw = distances[w];
                    if (tw != INF) {
                        float eikonal_d = update_triangle(pos_v, pos_u, vertices[w], distances[u], tw);
                        new_d = std::min(new_d, eikonal_d);
                    }
                }

                if (new_d < distances[v]) {
                    distances[v] = new_d;
                    pq.push({new_d, v});
                }
            }

            int tw = twin[curr_he];
            if (tw == -1) {
                hit_boundary = true;
                break;
            }
            curr_he = next(tw);
            if (++steps > n_he) break;
        } while (curr_he != start_he);

        if (hit_boundary) {
            int prev_he = prev(start_he);
            if (prev_he >= 0 && prev_he < n_he) {
                curr_he = twin[prev_he];
                steps = 0;
                while (curr_he != -1 && curr_he != start_he) {
                    if (curr_he < 0 || curr_he >= n_he) break;
                    int v = he_to_vertex[next(curr_he)];

                    if (v >= 0 && v < n && !vis[v]) {
                        float edge_len = glm::distance(vertices[u], vertices[v]);
                        if (distances[u] + edge_len < distances[v]) {
                            distances[v] = distances[u] + edge_len;
                            pq.push({distances[v], v});
                        }
                    }

                    int p = prev(curr_he);
                    if (p < 0 || p >= n_he) break;
                    curr_he = twin[p];
                    if (++steps > n_he) break;
                }
            }
        }
    }

    return distances;
}

struct FlatEdgeMap {
    struct Entry {
        uint64_t key = 0;
        int val = -1;
    };

    std::vector<Entry> table;
    size_t mask;

    explicit FlatEdgeMap(size_t expected_elements) {
        size_t cap = 1;
        while (cap < expected_elements * 2) cap <<= 1;
        table.resize(cap);
        mask = cap - 1;
    }

    static uint64_t hash_key(uint64_t k) {
        k ^= k >> 30;
        k *= 0xbf58476d1ce4e5b9ULL;
        k ^= k >> 27;
        k *= 0x94d049bb133111ebULL;
        k ^= k >> 31;
        return k;
    }

    void insert(uint64_t key, int val) {
        uint64_t stored_key = key + 1;
        size_t idx = hash_key(stored_key) & mask;
        while (table[idx].key != 0) {
            idx = (idx + 1) & mask;
        }
        table[idx].key = stored_key;
        table[idx].val = val;
    }

    int find(uint64_t key) const {
        uint64_t stored_key = key + 1;
        size_t idx = hash_key(stored_key) & mask;
        while (table[idx].key != 0) {
            if (table[idx].key == stored_key) return table[idx].val;
            idx = (idx + 1) & mask;
        }
        return -1;
    }
};

HalfEdgeContainer GeometryUtils::buildHalfEdge(const MeshData &m) {
    const size_t num_indices = m.indices.size();
    const size_t num_vertices = m.vertices.size();

    if (num_indices % 3 != 0 || num_vertices == 0) return {};

    std::vector<int> he_to_vertex(num_indices);
    std::vector<int> vertex_to_he(num_vertices, -1);
    std::vector<int> twin(num_indices, -1);

    FlatEdgeMap edge_to_he(num_indices);

    auto make_key = [](uint32_t u, uint32_t v) -> uint64_t {
        return (static_cast<uint64_t>(u) << 32) | static_cast<uint64_t>(v);
    };

    for (size_t i = 0; i < num_indices; i += 3) {
        int v0 = m.indices[i];
        int v1 = m.indices[i + 1];
        int v2 = m.indices[i + 2];

        he_to_vertex[i]     = v0;
        he_to_vertex[i + 1] = v1;
        he_to_vertex[i + 2] = v2;

        vertex_to_he[v0] = static_cast<int>(i);
        vertex_to_he[v1] = static_cast<int>(i + 1);
        vertex_to_he[v2] = static_cast<int>(i + 2);

        edge_to_he.insert(make_key(v0, v1), static_cast<int>(i));
        edge_to_he.insert(make_key(v1, v2), static_cast<int>(i + 1));
        edge_to_he.insert(make_key(v2, v0), static_cast<int>(i + 2));
    }

    for (size_t i = 0; i < num_indices; ++i) {
        int u = he_to_vertex[i];

        size_t mod3 = i % 3;
        int next_he = (mod3 == 2) ? static_cast<int>(i) - 2 : static_cast<int>(i) + 1;

        int v = he_to_vertex[next_he];

        int tw = edge_to_he.find(make_key(v, u));
        if (tw != -1) {
            twin[i] = tw;
        }
    }

    return HalfEdgeContainer{m.vertices, vertex_to_he, he_to_vertex, twin};
}

MeshData GeometryUtils::buildMeshData(const HalfEdgeContainer &he) {
    MeshData m;
    const int n_v = he.n_vertices();
    const int n_he = he.n_hes();

    m.vertices.reserve(n_v);
    m.uvs.reserve(n_v);
    m.indices.reserve(n_he);

    std::vector<int> v_remap(n_v, -1);
    int new_v_count = 0;

    const int num_faces = n_he / 3;
    for (int f = 0; f < num_faces; ++f) {
        int h0 = 3 * f;
        int u0 = he.he_to_vertex[h0];
        int u1 = he.he_to_vertex[h0 + 1];
        int u2 = he.he_to_vertex[h0 + 2];

        if (u0 != -1 && u1 != -1 && u2 != -1 && u0 != u1 && u1 != u2 && u2 != u0) {
            int idxs[3] = {u0, u1, u2};
            for (int k = 0; k < 3; ++k) {
                int old_v = idxs[k];
                if (v_remap[old_v] == -1) {
                    v_remap[old_v] = new_v_count++;
                    m.vertices.push_back(he.vertices[old_v]);
                    m.uvs.emplace_back(0.0f, 0.0f);
                }
                m.indices.push_back(v_remap[old_v]);
            }
        }
    }
    return m;
}