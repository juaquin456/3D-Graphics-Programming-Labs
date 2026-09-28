#ifndef MESH_SIMPLIFIER_H
#define MESH_SIMPLIFIER_H

#include <common/HalfEdgeMesh.h>
#include <vector>
#include <glm/glm.hpp>

class MeshSimplifier {
public:
    explicit MeshSimplifier(HalfEdgeContainer& mesh);

    void simplify(int edges_to_remove);

private:
    HalfEdgeContainer& mesh;

    void compute_target(int u, int v, const std::vector<glm::mat4>& Q,
                        glm::vec4& out_vp, float& out_err) const;
};

#endif // MESH_SIMPLIFIER_H
