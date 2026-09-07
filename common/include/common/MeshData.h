#ifndef COMMON_MESH_H
#define COMMON_MESH_H

#include <string>
#include <vector>
#include <glm/vec3.hpp>

struct MeshData {
    std::vector<float> vertices; // 3 * nvertices
    std::vector<int> indices;    // 3 * faces

    MeshData() = default;

    void save(const std::string& filename) const;
    std::pair<glm::vec3, glm::vec3> bounding_box() const;
};

MeshData readPly(const std::string& filename);

MeshData NewSphere(float radius, int slices, int stacks);
MeshData NewCube(float size);

#endif // COMMON_MESH_H