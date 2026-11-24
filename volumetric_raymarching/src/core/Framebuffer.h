#pragma once

#include <glad/glad.h>
#include <iostream>

namespace vrm {

class Framebuffer {
public:
    Framebuffer(int width = 1280, int height = 720, bool hasDepth = true, bool hdr = true);
    ~Framebuffer();

    void resize(int width, int height);
    void bind() const;
    void unbind() const;

    GLuint getColorTexture() const { return m_colorTexture; }
    GLuint getDepthTexture() const { return m_depthTexture; }
    GLuint getFBO() const { return m_fbo; }

    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }

private:
    int m_width;
    int m_height;
    bool m_hasDepth;
    bool m_hdr;

    GLuint m_fbo;
    GLuint m_colorTexture;
    GLuint m_depthTexture;

    void init();
    void cleanup();
};

} // namespace vrm
