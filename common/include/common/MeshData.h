#ifndef COMMON_MESH_H
#define COMMON_MESH_H

#include <string>
#include <vector>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct MeshData {
    std::vector<glm::vec3> vertices; // 3 * nvertices
    std::vector<int> indices;    // 3 * faces
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> normals;

    void save(const std::string& filename) const;
    std::pair<glm::vec3, glm::vec3> bounding_box() const;
    void recompute_normals();

    static MeshData readPly(const std::string& filename);
    static MeshData NewSphere(float radius, int slices, int stacks);
    static MeshData NewCube(float size);
};



#endif // COMMON_MESH_H