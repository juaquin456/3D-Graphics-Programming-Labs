//
// Created by juaquin-remon on 9/12/26.
//

#ifndef COMMON_SHADOWMAP_H
#define COMMON_SHADOWMAP_H

#include <glad/glad.h>
#include <iostream>

class ShadowMap {
public:
    unsigned int fbo{0};
    unsigned int depthTexture{0};
    unsigned int width{2048};
    unsigned int height{2048};

    ShadowMap() = default;
    ~ShadowMap();

    bool init(unsigned int shadowWidth = 2048, unsigned int shadowHeight = 2048);

    void bindForWriting() const;

    void unbind(unsigned int screenWidth, unsigned int screenHeight) const;

    void bindTexture(unsigned int textureUnit = 1) const;
};

#endif // COMMON_SHADOWMAP_H