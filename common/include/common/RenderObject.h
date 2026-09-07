//
// Created by juaquin-remon on 9/6/26.
//

#ifndef ANIMATION_RENDEROBJECT_H
#define ANIMATION_RENDEROBJECT_H
#include "MeshAsset.h"
#include "glm/fwd.hpp"


class RenderObject {
public:
    MeshAsset::Ptr meshAsset;

    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 color{1.0f};

    RenderObject(MeshAsset::Ptr asset) : meshAsset(asset) {}

    glm::mat4 getModelMatrix() const;

    void draw(GLint modelLoc, GLint colorLoc = -1) const;
};
#endif //ANIMATION_RENDEROBJECT_H