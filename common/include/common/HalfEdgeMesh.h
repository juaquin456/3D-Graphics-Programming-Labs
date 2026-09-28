#ifndef COMMON_HALFEDGEMESH_H
#define COMMON_HALFEDGEMESH_H

#include <vector>
#include "common/MeshData.h"
#include <glm/glm.hpp>

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
    std::vector<glm::vec3> vertices;   // unique positions
    std::vector<int> vertex_to_he;     // unique positions count
    std::vector<int> he_to_vertex;     // 3 * faces = halfedges
    std::vector<int> twin;             // 3 * faces = halfedges
    std::vector<int> orig_to_pos;      // maps original mesh vertex index -> unique pos index

    [[nodiscard]] int n_vertices() const;
    [[nodiscard]] int n_hes() const;

    [[nodiscard]] glm::vec3 get_vertex_pos(int v_idx) const;
    [[nodiscard]] glm::vec3 get_vertex(int he) const;
    [[nodiscard]] int get_he(int u, int v) const;
    [[nodiscard]] std::vector<int> get_neighbors(int u) const;

    [[nodiscard]] std::vector<float> compute_fast_marching_distances(int start_vertex) const;
};

namespace GeometryUtils {
    HalfEdgeContainer buildHalfEdge(const MeshData& mesh);
    MeshData buildMeshData(const HalfEdgeContainer& he);
}
#endif // COMMON_HALFEDGEMESH_H