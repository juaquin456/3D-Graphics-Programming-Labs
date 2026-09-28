#include "common/MeshSimplifier.h"
#include "common/Queue.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#include <limits>
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/compatibility.hpp"

MeshSimplifier::MeshSimplifier(HalfEdgeContainer& mesh) : mesh(mesh) {}
bool solve_qem_target(const glm::mat4& Q, glm::vec3& out_p) {
    float m[3][4] = {
        { Q[0][0], Q[1][0], Q[2][0], -Q[3][0] },
        { Q[1][0], Q[1][1], Q[2][1], -Q[3][1] },
        { Q[2][0], Q[2][1], Q[2][2], -Q[3][2] }
    };

    float max_value = 0.0f;

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            max_value = std::max(
                max_value,
                std::abs(m[row][col])
            );
        }
    }

    if (max_value <= 1e-12f) {
        return false;
    }

    const float tolerance = max_value * 1e-6f;

    for (int col = 0; col < 3; ++col) {
        int pivot = col;
        float pivot_abs = std::abs(m[col][col]);

        for (int row = col + 1; row < 3; ++row) {
            float value = std::abs(m[row][col]);

            if (value > pivot_abs) {
                pivot = row;
                pivot_abs = value;
            }
        }

        if (pivot_abs <= tolerance) {
            return false;
        }

        if (pivot != col) {
            for (int j = col; j < 4; ++j) {
                std::swap(m[col][j], m[pivot][j]);
            }
        }

        for (int row = col + 1; row < 3; ++row) {
            const float factor = m[row][col] / m[col][col];

            for (int j = col; j < 4; ++j) {
                m[row][j] -= factor * m[col][j];
            }
        }
    }

    glm::vec3 p;

    p.z = m[2][3] / m[2][2];

    p.y = (
        m[1][3] - m[1][2] * p.z
    ) / m[1][1];

    p.x = (
        m[0][3]
        - m[0][1] * p.y
        - m[0][2] * p.z
    ) / m[0][0];

    if (!glm::all(glm::isfinite(p))) {
        return false;
    }

    out_p = p;
    return true;
}
float evaluate_quadric(const glm::mat4& Q, const glm::vec3& p) {
    const float x = p.x;
    const float y = p.y;
    const float z = p.z;

    return
        Q[0][0] * x * x +
        2.0f * Q[1][0] * x * y +
        2.0f * Q[2][0] * x * z +
        2.0f * Q[3][0] * x +
        Q[1][1] * y * y +
        2.0f * Q[2][1] * y * z +
        2.0f * Q[3][1] * y +
        Q[2][2] * z * z +
        2.0f * Q[3][2] * z +
        Q[3][3];
}
void MeshSimplifier::compute_target(
    int u,
    int v,
    const std::vector<glm::mat4>& Q,
    glm::vec4& out_vp,
    float& out_err
) const {
    const glm::mat4 Qp = Q[u] + Q[v];

    glm::vec3 p;

    if (solve_qem_target(Qp, p)) {
        out_vp = glm::vec4(p, 1.0f);

        out_err =
            Qp[3][0] * p.x +
            Qp[3][1] * p.y +
            Qp[3][2] * p.z +
            Qp[3][3];

        if (!std::isfinite(out_err) || out_err < 0.0f) {
            out_err = 0.0f;
        }

        return;
    } else {
        const glm::vec3 p_u = mesh.get_vertex_pos(u);
        const glm::vec3 p_v = mesh.get_vertex_pos(v);

        const glm::vec3 edge = p_v - p_u;
        const float edge_len2 = glm::dot(edge, edge);

        const float max_distance2 = edge_len2 * 4.0f;

        const float dist_u2 = glm::dot(p - p_u, p - p_u);
        const float dist_v2 = glm::dot(p - p_v, p - p_v);

        if (dist_u2 > max_distance2 ||
            dist_v2 > max_distance2) {
            }
    }

    const glm::vec3 p_u = mesh.get_vertex_pos(u);
    const glm::vec3 p_v = mesh.get_vertex_pos(v);
    const glm::vec3 p_mid = (p_u + p_v) * 0.5f;

    const glm::vec3 candidates[3] = {
        p_u,
        p_v,
        p_mid
    };

    float min_e = std::numeric_limits<float>::max();
    glm::vec3 best_p = p_u;

    for (const glm::vec3& candidate : candidates) {
        const float e = evaluate_quadric(Qp, candidate);

        if (e < min_e) {
            min_e = e;
            best_p = candidate;
        }
    }

    out_vp = glm::vec4(best_p, 1.0f);
    out_err = min_e;

    if (!std::isfinite(out_err) || out_err < 0.0f) {
        out_err = 0.0f;
    }
}

void MeshSimplifier::simplify(int edges_to_remove) {
    int num_vertices = mesh.n_vertices();
    int num_hes = mesh.n_hes();

    std::vector<glm::mat4> Q(num_vertices, glm::mat4(0.0f));

    for (int f = 0; f < num_hes / 3; ++f) {
        int h0 = 3 * f;
        int u0 = mesh.he_to_vertex[h0];
        int u1 = mesh.he_to_vertex[h0 + 1];
        int u2 = mesh.he_to_vertex[h0 + 2];

        if (u0 == -1 || u1 == -1 || u2 == -1) continue;

        glm::vec3 p0 = mesh.get_vertex_pos(u0);
        glm::vec3 p1 = mesh.get_vertex_pos(u1);
        glm::vec3 p2 = mesh.get_vertex_pos(u2);

        glm::vec3 cr = glm::cross(p1 - p0, p2 - p0);
        float area = glm::length(cr);

        if (area > 1e-8f) {
            glm::vec3 normal = cr / area;
            float d = -glm::dot(normal, p0);
            glm::vec4 p{normal.x, normal.y, normal.z, d};
            glm::mat4 Q_face = glm::outerProduct(p, p);

            Q[u0] += Q_face;
            Q[u1] += Q_face;
            Q[u2] += Q_face;
        }
    }

    QueueSystem q;

    for (int he = 0; he < num_hes; he++) {
        int u = mesh.he_to_vertex[he];
        int v = mesh.he_to_vertex[next(he)];

        if (u != -1 && v != -1 && u < v) {
            glm::vec4 vp;
            float err;
            compute_target(u, v, Q, vp, err);

            q.push_or_update(u, v, err, vp);
        }
    }
    std::vector<int> neighbor_marks(mesh.n_vertices(), 0);
    int neighbor_stamp = 0;
    auto link_twins = [&](int a, int b) {
        if (a != -1) mesh.twin[a] = b;
        if (b != -1) mesh.twin[b] = a;
    };
    int removed = 0;
    std::vector<int> hes_from_v;
    hes_from_v.reserve(16);
    std::vector<int> n_u, n_v;
    while (!q.empty() && removed < edges_to_remove) {
        queueData top;
        if (!q.pop(top)) break;

        int u = top.u;
        int v = top.v;

        if (mesh.vertex_to_he[u] == -1 || mesh.vertex_to_he[v] == -1) continue;

        mesh.get_neighbors(u, n_u);
        mesh.get_neighbors(v, n_v);
        ++neighbor_stamp;
        for (int n: n_u) {
            neighbor_marks[n] = neighbor_stamp;
        }
        int common_neighbors = 0;

        for (int n : n_v) {
            if (neighbor_marks[n] == neighbor_stamp) {
                ++common_neighbors;
            }
        }
        if (common_neighbors > 2) {
            q.erase_edge(u, v);
            continue;
        }

        int he_uv = mesh.get_he(u, v);
        int he_vu = mesh.get_he(v, u);

        if (he_uv == -1 && he_vu == -1) {
            q.erase_edge(u, v);
            continue;
        }

        hes_from_v.clear();
        int start_he_v = mesh.vertex_to_he[v];
        if (start_he_v != -1 && mesh.he_to_vertex[start_he_v] == v) {
            int curr = start_he_v;
            int steps = 0;
            bool hit_boundary = false;
            do {
                if (curr == -1 || mesh.he_to_vertex[curr] != v) break;
                hes_from_v.push_back(curr);
                int tw = mesh.twin[curr];
                if (tw == -1) {
                    hit_boundary = true;
                    break;
                }
                curr = next(tw);
                if (++steps > num_hes) break;
            } while (curr != start_he_v);

            if (hit_boundary) {
                int p_he = prev(start_he_v);
                if (p_he != -1) {
                    curr = mesh.twin[p_he];
                    steps = 0;
                    while (curr != -1 && curr != start_he_v) {
                        if (mesh.he_to_vertex[curr] == v) {
                            hes_from_v.push_back(curr);
                        }
                        int p = prev(curr);
                        if (p == -1) break;
                        curr = mesh.twin[p];
                        if (++steps > num_hes) break;
                    }
                }
            }
        }

        mesh.vertices[u] = glm::vec3(top.vp);
        Q[u] = Q[u] + Q[v];

        auto collapse_face = [&](int he) {
            if (he == -1) return;
            int h_next = next(he);
            int h_prev = prev(he);

            int w = mesh.he_to_vertex[h_prev];

            int t_next = mesh.twin[h_next];
            int t_prev = mesh.twin[h_prev];

            link_twins(t_next, t_prev);

            mesh.he_to_vertex[he]     = -1;
            mesh.he_to_vertex[h_next] = -1;
            mesh.he_to_vertex[h_prev] = -1;

            mesh.twin[he]     = -1;
            mesh.twin[h_next] = -1;
            mesh.twin[h_prev] = -1;

            if (w != -1 && w != u && w != v) {
                if (mesh.vertex_to_he[w] == h_prev || mesh.vertex_to_he[w] == h_next) {
                    mesh.vertex_to_he[w] = (t_next != -1) ? t_next : t_prev;
                }
            }
        };

        collapse_face(he_uv);
        collapse_face(he_vu);

        int valid_he_for_u = -1;
        for (int h : hes_from_v) {
            if (mesh.he_to_vertex[h] != -1) {
                mesh.he_to_vertex[h] = u;
                valid_he_for_u = h;
            }
        }

        if (valid_he_for_u != -1 && mesh.he_to_vertex[valid_he_for_u] == u) {
            mesh.vertex_to_he[u] = valid_he_for_u;
        }

        mesh.vertex_to_he[v] = -1;

        q.erase_edge(u, v);
        for (int n : n_v) {
            if (n == u || n == -1) continue;

            q.erase_edge(v, n);
        }

        mesh.get_neighbors(u, n_u);

        for (int n : n_u) {
            if (n == u || n == -1) continue;

            glm::vec4 new_vp;
            float new_err;

            compute_target(u, n, Q, new_vp, new_err);

            q.push_or_update(u, n, new_err, new_vp);
        }
        removed++;
    }
}