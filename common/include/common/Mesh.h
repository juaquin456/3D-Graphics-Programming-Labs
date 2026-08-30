#ifndef COMMON_MESH_H
#define COMMON_MESH_H

#include <string>
#include <vector>

struct Mesh {
    std::vector<float> vertices; // 3 * nvertices
    std::vector<int> indices;    // 3 * faces
    explicit Mesh(const std::string& filename);
    explicit Mesh() = default;
    void save(const std::string& filename) const;
};

Mesh NewSphere(float radius, int slices, int stacks);

#endif // COMMON_MESH_H