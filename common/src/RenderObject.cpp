//
// Created by juaquin-remon on 9/6/26.
//

#include "common/RenderObject.h"
#include <glm/gtc/type_ptr.hpp>

void RenderObject::rotateAxis(float angleDegrees, const glm::vec3 &axis) {
    glm::quat deltaRot = glm::angleAxis(glm::radians(angleDegrees), glm::normalize(axis));
    rotation = glm::normalize(deltaRot * rotation);
}

glm::mat4 RenderObject::getModelMatrix() const {
    glm::mat4 model(1.0f);
    model = glm::translate(model, position);
    model = glm::scale(model, scale);
    model = model * glm::mat4_cast(rotation);
    if (meshAsset) {
        model = glm::scale(model, glm::vec3(meshAsset->autoScaleFactor));
        model = glm::translate(model, -meshAsset->localCenter);
    }
    return model;
}

void RenderObject::setRotationEuler(float pitchDeg, float yawDeg, float rollDeg) {
    rotation = glm::quat(glm::vec3(glm::radians(pitchDeg), glm::radians(yawDeg), glm::radians(rollDeg)));
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

void RenderObject::drawGeometry(const Shader &shader) const {
    if (!meshAsset) return;

    glm::mat4 model = getModelMatrix();
    glUniformMatrix4fv(glGetUniformLocation(shader.ID, "model"), 1, GL_FALSE, glm::value_ptr(model));
    meshAsset->draw();
}

glm::mat4 DirectionalLight::getLightSpaceMatrix(float orthoSize, float nearPlane, float farPlane) const {
    glm::mat4 lightView = glm::lookAt(position,
                                  target,
                                  glm::vec3( 0.0f, 1.0f,  0.0f));
    glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, nearPlane, farPlane);

    return lightProjection * lightView;
}

void DirectionalLight::bind(const Shader &shader) const {
    shader.setVec3("light.position", position);
    shader.setVec3("light.color", color);
    shader.setMat4("lightSpaceMatrix", getLightSpaceMatrix());
}
