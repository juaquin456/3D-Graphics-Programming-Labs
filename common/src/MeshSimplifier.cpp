#include "common/MeshSimplifier.h"
#include "common/Queue.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#include <limits>

MeshSimplifier::MeshSimplifier(HalfEdgeContainer& mesh) : mesh(mesh) {}

void MeshSimplifier::compute_target(int u, int v, const std::vector<glm::mat4>& Q,
                                    glm::vec4& out_vp, float& out_err) const {
    glm::mat4 Qp = Q[u] + Q[v];
    glm::mat4 Qp_inv = Qp;

    Qp_inv[0].w = 0.0f;
    Qp_inv[1].w = 0.0f;
    Qp_inv[2].w = 0.0f;
    Qp_inv[3].w = 1.0f;

    float det = glm::determinant(Qp_inv);
    if (std::abs(det) > 1e-6f) {
        auto inv = glm::inverse(Qp_inv);
        out_vp = inv * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
        out_vp.w = 1.0f;
    } else {
        glm::vec3 p_u = mesh.get_vertex_pos(u);
        glm::vec3 p_v = mesh.get_vertex_pos(v);
        glm::vec3 p_mid = (p_u + p_v) * 0.5f;

        glm::vec4 candidates[3] = { glm::vec4{p_u, 1.0f}, glm::vec4{p_v, 1.0f}, glm::vec4{p_mid, 1.0f} };
        float min_e = 1e30f;
        auto best_vp = candidates[0];

        for (int k = 0; k < 3; ++k) {
            float e = glm::dot(candidates[k], Qp * candidates[k]);
            if (e < min_e) {
                min_e = e;
                best_vp = candidates[k];
            }
        }
        out_vp = best_vp;
    }

    out_err = glm::dot(out_vp, Qp * out_vp);
    if (out_err < 0.0f || std::isnan(out_err)) out_err = 0.0f;
}

void MeshSimplifier::simplify(int edges_to_remove) {
    int num_vertices = mesh.n_vertices();
    int num_hes = mesh.n_hes();

    std::vector<glm::mat4> Q(num_vertices, glm::mat4(0.0f));

    for (int i = 0; i < num_vertices; i++) {
        int init_he = mesh.vertex_to_he[i];
        if (init_he == -1) continue;

        glm::mat4 sumQ(0.0f);

        auto process_face = [&](int he) {
            int he0 = he;
            int he1 = next(he0);
            int he2 = next(he1);

            auto v0 = mesh.get_vertex(he0);
            auto v1 = mesh.get_vertex(he1);
            auto v2 = mesh.get_vertex(he2);

            glm::vec3 cr = glm::cross(v1 - v0, v2 - v0);
            float area = glm::length(cr);

            if (area > 1e-8f) {
                glm::vec3 normal = cr / area;
                float d = -glm::dot(normal, v0);
                glm::vec4 p{normal.x, normal.y, normal.z, d};
                sumQ += glm::outerProduct(p, p);
            }
        };

        int current_he = init_he;
        bool hit_boundary = false;
        do {
            process_face(current_he);
            int tw = mesh.twin[current_he];
            if (tw == -1) {
                hit_boundary = true;
                break;
            }
            current_he = next(tw);
        } while (current_he != init_he && current_he != -1);

        if (hit_boundary) {
            int p_he = prev(init_he);
            if (p_he != -1) {
                current_he = mesh.twin[p_he];
                while (current_he != -1 && current_he != init_he) {
                    process_face(current_he);
                    int p = prev(current_he);
                    current_he = (p != -1) ? mesh.twin[p] : -1;
                }
            }
        }

        Q[i] = sumQ;
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

    auto link_twins = [&](int a, int b) {
        if (a != -1) mesh.twin[a] = b;
        if (b != -1) mesh.twin[b] = a;
    };

    int removed = 0;
    while (!q.empty() && removed < edges_to_remove) {
        queueData top;
        if (!q.pop(top)) break;

        int u = top.u;
        int v = top.v;

        if (mesh.vertex_to_he[u] == -1 || mesh.vertex_to_he[v] == -1) continue;

        auto n_u = mesh.get_neighbors(u);
        auto n_v = mesh.get_neighbors(v);
        int common_neighbors = 0;
        for (int nu : n_u) {
            for (int nv : n_v) {
                if (nu == nv) common_neighbors++;
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

        std::vector<int> hes_from_v;
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
        std::vector<int> neighbors = mesh.get_neighbors(u);

        for (int n : neighbors) {
            if (n == u || n == -1) continue;

            q.erase_edge(v, n);

            glm::vec4 new_vp;
            float new_err;
            compute_target(u, n, Q, new_vp, new_err);

            q.push_or_update(u, n, new_err, new_vp);
        }

        removed++;
    }
}