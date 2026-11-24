#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace vrm {

class ShadowMap {
public:
    ShadowMap(int resolution = 1024);
    ~ShadowMap();

    void init();
    void bind() const;
    void unbind() const;

    GLuint getDepthTexture() const { return m_depthTexture; }
    int getResolution() const { return m_resolution; }

    glm::mat4 calculateLightSpaceMatrix(const glm::vec3& lightPos,
                                         const glm::vec3& lightTarget,
                                         float cutoffAngleDeg = 40.0f,
                                         float nearPlane = 0.5f,
                                         float farPlane = 15.0f);

    glm::mat4 getLightSpaceMatrix() const { return m_lightSpaceMatrix; }

private:
    int m_resolution;
    GLuint m_fbo;
    GLuint m_depthTexture;
    glm::mat4 m_lightSpaceMatrix;

    void cleanup();
};

} // namespace vrm
