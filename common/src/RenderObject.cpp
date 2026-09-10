//
// Created by juaquin-remon on 9/6/26.
//

#include "common/RenderObject.h"
#include <glm/gtc/type_ptr.hpp>

#include "common/Shader.h"

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

void RenderObject::draw(const Shader& shader) const {
    if (!meshAsset) return;

    glm::mat4 model = getModelMatrix();
    glUniformMatrix4fv(glGetUniformLocation(shader.ID, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform3fv(glGetUniformLocation(shader.ID, "material.Ka"), 1, glm::value_ptr(material.Ka));
    glUniform3fv(glGetUniformLocation(shader.ID, "material.Kd"), 1, glm::value_ptr(material.Kd));
    glUniform3fv(glGetUniformLocation(shader.ID, "material.Ks"), 1, glm::value_ptr(material.Ks));
    glUniform1f(glGetUniformLocation(shader.ID, "material.shininess"), material.shininess);

    meshAsset->draw();
}
