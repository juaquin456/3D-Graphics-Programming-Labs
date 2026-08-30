#ifndef MESH_SIMPLIFIER_H
#define MESH_SIMPLIFIER_H

#include <common/HalfEdgeMesh.h>
#include <common/Mesh.h>
#include "linalg.h"
#include <vector>

class MeshSimplifier {
public:
    explicit MeshSimplifier(HalfEdgeContainer& mesh);

    void simplify(int edges_to_remove);

private:
    HalfEdgeContainer& mesh;

    void compute_target(int u, int v, const std::vector<linalg::aliases::float4x4>& Q,
                        linalg::aliases::float4& out_vp, float& out_err) const;
};

#endif // MESH_SIMPLIFIER_H
