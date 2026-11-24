#include "ShadowMap.h"
#include <iostream>

namespace vrm {

ShadowMap::ShadowMap(int resolution)
    : m_resolution(resolution),
      m_fbo(0),
      m_depthTexture(0),
      m_lightSpaceMatrix(1.0f)
{
    // Note: init() must be called explicitly after OpenGL context / GLAD is loaded!
}

ShadowMap::~ShadowMap() {
    cleanup();
}

void ShadowMap::cleanup() {
    if (m_depthTexture) {
        glDeleteTextures(1, &m_depthTexture);
        m_depthTexture = 0;
    }
    if (m_fbo) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
}

void ShadowMap::init() {
    cleanup();

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_depthTexture);
    glBindTexture(GL_TEXTURE_2D, m_depthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_resolution, m_resolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    // Border color 1.0 (far plane) so objects outside shadow frustum are not shadowed
    float borderColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTexture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[ShadowMap ERROR] Framebuffer is not complete!" << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

glm::mat4 ShadowMap::calculateLightSpaceMatrix(const glm::vec3& lightPos,
                                               const glm::vec3& lightTarget,
                                               float cutoffAngleDeg,
                                               float nearPlane,
                                               float farPlane)
{
    glm::mat4 lightProjection = glm::perspective(
        glm::radians(cutoffAngleDeg * 2.2f),
        1.0f,
        nearPlane,
        farPlane
    );

    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 dir = glm::normalize(lightTarget - lightPos);
    if (std::abs(dir.y) > 0.99f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    glm::mat4 lightView = glm::lookAt(lightPos, lightTarget, up);
    m_lightSpaceMatrix = lightProjection * lightView;
    return m_lightSpaceMatrix;
}

void ShadowMap::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_resolution, m_resolution);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void ShadowMap::unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace vrm
