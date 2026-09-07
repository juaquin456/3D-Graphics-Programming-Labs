//
// Created by juaquin-remon on 9/6/26.
//

#include "common/RenderObject.h"
#include <glm/gtc/type_ptr.hpp>
glm::mat4 RenderObject::getModelMatrix() const {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = glm::scale(model, scale);

    if (meshAsset) {
        model = glm::scale(model, glm::vec3(meshAsset->autoScaleFactor));
        model = glm::translate(model, -meshAsset->localCenter);
    }
    return model;
}

void RenderObject::draw(GLint modelLoc, GLint colorLoc) const {
    if (!meshAsset) return;

    glm::mat4 model = getModelMatrix();
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

    if (colorLoc != -1) {
        glUniform3fv(colorLoc, 1, glm::value_ptr(color));
    }

    meshAsset->draw();
}
