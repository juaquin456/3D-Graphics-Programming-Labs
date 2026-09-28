//
// Created by juaquin on 9/17/26.
//

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "common/Texture.h"

Texture::Texture(const std::string& filename){
    stbi_set_flip_vertically_on_load(true);
    int width, height, channels;
    unsigned char* data = stbi_load(filename.c_str(), &width, &height, &channels, 0);
    if (data == nullptr) {
        std::cerr << "[Texture] Error: Failed to load texture from " << filename << std::endl;
        return;
    }
    GLenum format = GL_RGB;
    GLenum internalFormat = GL_RGB8;

    if (channels == 1) {
        format = GL_RED;
        internalFormat = GL_R8;
    } else if (channels == 2) {
        format = GL_RG;
        internalFormat = GL_RG8;
    } else if (channels == 3) {
        format = GL_RGB;
        internalFormat = GL_RGB8;
    } else if (channels == 4) {
        format = GL_RGBA;
        internalFormat = GL_RGBA8;
    }
    std::cout << "[Texture] Loaded texture: " << filename
              << " (Width: " << width << ", Height: " << height << ", Channels: " << channels << ")" << std::endl;
    init(width, height, data, format, internalFormat);
    stbi_image_free(data);
}
void Texture::init(int width, int height, const unsigned char* data, GLenum format, GLenum internalFormat) {
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (format == GL_RED) {
        GLint swizzleMask[] = { GL_RED, GL_RED, GL_RED, GL_ONE };
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

Texture::Ptr Texture::White() {
    static Texture::Ptr whiteTex = nullptr;

    if (!whiteTex) {
        whiteTex = std::make_shared<Texture>();
        unsigned char whitePixel[4] = { 255, 255, 255, 255 };
        whiteTex->init(1, 1, whitePixel, GL_RGBA, GL_RGBA8);
    }

    return whiteTex;
}

Texture::Ptr Texture::DefaultNormal() {
    static Texture::Ptr normalTex = nullptr;

    if (!normalTex) {
        normalTex = std::make_shared<Texture>();
        unsigned char normalPixel[3] = { 128, 128, 255 };
        normalTex->init(1, 1, normalPixel, GL_RGB, GL_RGB8);
    }

    return normalTex;
}