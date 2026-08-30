#include "MeshSimplifier.h"
#include "Queue.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using namespace linalg::aliases;

MeshSimplifier::MeshSimplifier(HalfEdgeContainer& mesh) : mesh(mesh) {}

void MeshSimplifier::compute_target(int u, int v, const std::vector<float4x4>& Q,
                                    float4& out_vp, float& out_err) const {
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
        float3 p_u = mesh.get_vertex_pos(u);
        float3 p_v = mesh.get_vertex_pos(v);
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

void MeshSimplifier::simplify(int edges_to_remove) {
    int num_vertices = mesh.n_vertices();
    int num_hes = mesh.n_hes();

    std::vector<float4x4> Q(num_vertices);
    for (int i = 0; i < num_vertices; i++) {
        float4x4 sumQ;
        int init_he = mesh.vertex_to_he[i];
        if (init_he == -1) continue;
        int current_he = init_he;
        do {
            int he0 = current_he;
            int he1 = next(he0);
            int he2 = next(he1);
            auto v0 = mesh.get_vertex(he0);
            auto v1 = mesh.get_vertex(he1);
            auto v2 = mesh.get_vertex(he2);

            auto normal = normalize(cross(v1 - v0, v2 - v0));
            auto d = -dot(normal, v0);
            linalg::mat<float, 1, 4> p{normal.x, normal.y, normal.z, d};
            sumQ += mul(linalg::transpose(p), p);

            current_he = mesh.twin[prev(current_he)];
        } while (current_he != -1 && current_he != init_he);

        Q[i] = sumQ;
    }

    QueueSystem q;
    for (int he = 0; he < num_hes; he++) {
        int u = mesh.he_to_vertex[he];
        int v = mesh.he_to_vertex[next(he)];

        if (u != -1 && v != -1 && u < v) {
            float4 vp;
            float err;
            compute_target(u, v, Q, vp, err);
            q.push_or_update(u, v, err, vp);
        }
    }

    auto set_twin = [&](int a, int b) {
        if (a != -1) {
            int old_a = mesh.twin[a];
            if (old_a != -1 && old_a != b) mesh.twin[old_a] = -1;
            mesh.twin[a] = b;
        }
        if (b != -1) {
            int old_b = mesh.twin[b];
            if (old_b != -1 && old_b != a) mesh.twin[old_b] = -1;
            mesh.twin[b] = a;
        }
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
        int he_vu = (he_uv != -1) ? mesh.twin[he_uv] : mesh.get_he(v, u);

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
                steps++;
            } while (curr != start_he_v && steps < 100);

            if (hit_boundary) {
                curr = mesh.twin[prev(start_he_v)];
                steps = 0;
                while (curr != -1 && curr != start_he_v && steps < 100) {
                    if (mesh.he_to_vertex[curr] == v) {
                        hes_from_v.push_back(curr);
                    }
                    curr = mesh.twin[prev(curr)];
                    steps++;
                }
            }
        }

        mesh.vertices[3 * u]     = top.vp.x;
        mesh.vertices[3 * u + 1] = top.vp.y;
        mesh.vertices[3 * u + 2] = top.vp.z;
        Q[u] = Q[u] + Q[v];

        auto collapse_face = [&](int he) {
            if (he == -1) return;
            int h_next = next(he);
            int h_prev = prev(he);

            int w = mesh.he_to_vertex[h_prev];

            int t_next = mesh.twin[h_next];
            int t_prev = mesh.twin[h_prev];

            if (t_next != -1 && t_prev != -1 && t_next != t_prev) {
                set_twin(t_next, t_prev);
            } else {
                if (t_next != -1) set_twin(t_next, -1);
                if (t_prev != -1) set_twin(t_prev, -1);
            }

            set_twin(he, -1);
            set_twin(h_next, -1);
            set_twin(h_prev, -1);

            mesh.he_to_vertex[he]     = -1;
            mesh.he_to_vertex[h_next] = -1;
            mesh.he_to_vertex[h_prev] = -1;

            if (w != -1 && w != u && w != v) {
                if (mesh.vertex_to_he[w] == h_prev || mesh.vertex_to_he[w] == -1 || mesh.he_to_vertex[mesh.vertex_to_he[w]] != w) {
                    if (t_next != -1 && mesh.he_to_vertex[t_next] == w) {
                        mesh.vertex_to_he[w] = t_next;
                    } else {
                        mesh.vertex_to_he[w] = -1;
                        for (int nw : mesh.get_neighbors(w)) {
                            int cand = mesh.get_he(w, nw);
                            if (cand != -1 && mesh.he_to_vertex[cand] == w) {
                                mesh.vertex_to_he[w] = cand;
                                break;
                            }
                        }
                    }
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
        } else if (mesh.vertex_to_he[u] == -1 || mesh.he_to_vertex[mesh.vertex_to_he[u]] != u) {
            mesh.vertex_to_he[u] = -1;
            for (int nu : mesh.get_neighbors(u)) {
                int cand = mesh.get_he(u, nu);
                if (cand != -1 && mesh.he_to_vertex[cand] == u) {
                    mesh.vertex_to_he[u] = cand;
                    break;
                }
            }
        }

        mesh.vertex_to_he[v] = -1;

        q.erase_edge(u, v);
        std::vector<int> neighbors = mesh.get_neighbors(u);

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
