//
// Created by juaquin on 9/17/26.
//

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "common/Texture.h"

Texture::Texture(const std::string& filename){
    int width, height, channels;
    unsigned char* data = stbi_load(filename.c_str(), &width, &height, &channels, 0);
    if (data == nullptr) {
        std::cout << "Error loading texture" << std::endl;
        return;
    }
    GLenum format;
    if (channels == 1)
        format = GL_RED;
    else if (channels== 3)
        format = GL_RGB;
    else if (channels== 4)
        format = GL_RGBA;
    std::cout << "Loading texture " << filename << std::endl;
    std::cout << "width: " << width << " height: " << height << " channels: " << channels << std::endl;
    init(width, height, data, format);
    stbi_image_free(data);
}
void Texture::init(int width, int height, const unsigned char* data, GLenum format) {
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (format == GL_RED) {
        GLint swizzleMask[] = { GL_RED, GL_RED, GL_RED, GL_ONE };
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
}
