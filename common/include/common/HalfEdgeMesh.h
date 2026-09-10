#ifndef COMMON_HALFEDGEMESH_H
#define COMMON_HALFEDGEMESH_H

#include <vector>
#include "common/MeshData.h"

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

struct HalfEdgeContainer {
    std::vector<glm::vec3> vertices;   // 3 * nvertices
    std::vector<int> vertex_to_he; // nvertices
    std::vector<int> he_to_vertex; // 3 * faces = halfedges
    std::vector<int> twin;         // 3 * faces = halfedges

    int n_vertices() const;
    int n_hes() const;

    glm::vec3 get_vertex_pos(int v_idx) const;
    glm::vec3 get_vertex(int he) const;
    int get_he(int u, int v) const;
    std::vector<int> get_neighbors(int u) const;
    MeshData to_mesh() const;
};

HalfEdgeContainer NewHalfEdgeContainer(const MeshData& m);

#endif // COMMON_HALFEDGEMESH_H