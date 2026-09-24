//
// Created by juaquin on 9/16/26.
//

#ifndef PLACING_CAMERA_TEXTURE_H
#define PLACING_CAMERA_TEXTURE_H


#include <iostream>
#include <memory>
#include <string>

#include "glad/glad.h"

struct Texture {
   using Ptr = std::shared_ptr<Texture>;

   unsigned int textureID{};
   Texture() = default;
   explicit Texture(const std::string& filename);

   void init(int width, int height, const unsigned char* data, GLenum format);
   Texture& operator=(Texture const& texture) = default;
   void bind(unsigned int unit = 0) const {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, textureID);
   }

   void unbind(unsigned int unit = 0) const {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, 0);
   }
};

#endif //PLACING_CAMERA_TEXTURE_H
