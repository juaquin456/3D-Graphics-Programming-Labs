#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <unordered_map>
#include <iostream>

namespace vrm {

class Shader {
public:
    Shader();
    ~Shader();

    bool loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath);
    bool loadFromSource(const std::string& vertexSource, const std::string& fragmentSource);

    void use() const;
    GLuint getID() const { return m_programId; }
    bool isValid() const { return m_programId != 0; }

    // Uniform helpers
    void setBool(const std::string& name, bool value) const;
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec2(const std::string& name, const glm::vec2& value) const;
    void setVec2(const std::string& name, float x, float y) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setVec3(const std::string& name, float x, float y, float z) const;
    void setVec4(const std::string& name, const glm::vec4& value) const;
    void setMat4(const std::string& name, const glm::mat4& mat) const;

private:
    GLuint m_programId;
    mutable std::unordered_map<std::string, GLint> m_uniformLocationCache;

    GLint getUniformLocation(const std::string& name) const;
    GLuint compileShader(GLenum type, const std::string& source, const std::string& debugName);
    std::string readFile(const std::string& filePath);
};

} // namespace vrm
