#include "Shader.h"
#include <fstream>
#include <sstream>
#include <vector>

namespace vrm {

Shader::Shader() : m_programId(0) {}

Shader::~Shader() {
    if (m_programId != 0) {
        glDeleteProgram(m_programId);
    }
}

std::string Shader::readFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[Shader ERROR] Failed to open file: " << filePath << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

GLuint Shader::compileShader(GLenum type, const std::string& source, const std::string& debugName) {
    if (source.empty()) {
        std::cerr << "[Shader ERROR] Source for " << debugName << " is empty!" << std::endl;
        return 0;
    }

    GLuint shader = glCreateShader(type);
    const char* srcPtr = source.c_str();
    glShaderSource(shader, 1, &srcPtr, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> infoLog(logLength);
        glGetShaderInfoLog(shader, logLength, nullptr, infoLog.data());
        std::cerr << "[Shader ERROR] Compilation failed for " << debugName << ":\n"
                  << infoLog.data() << std::endl;
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

bool Shader::loadFromSource(const std::string& vertexSource, const std::string& fragmentSource) {
    if (m_programId != 0) {
        glDeleteProgram(m_programId);
        m_programId = 0;
        m_uniformLocationCache.clear();
    }

    GLuint vert = compileShader(GL_VERTEX_SHADER, vertexSource, "VertexShader");
    if (!vert) return false;

    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragmentSource, "FragmentShader");
    if (!frag) {
        glDeleteShader(vert);
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> infoLog(logLength);
        glGetProgramInfoLog(program, logLength, nullptr, infoLog.data());
        std::cerr << "[Shader ERROR] Linking failed:\n" << infoLog.data() << std::endl;
        glDeleteProgram(program);
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);

    m_programId = program;
    return true;
}

bool Shader::loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath) {
    std::string vSource = readFile(vertexPath);
    std::string fSource = readFile(fragmentPath);

    if (vSource.empty() || fSource.empty()) {
        std::cerr << "[Shader ERROR] Unable to load shaders from paths:\n"
                  << "  Vertex:   " << vertexPath << "\n"
                  << "  Fragment: " << fragmentPath << std::endl;
        return false;
    }

    return loadFromSource(vSource, fSource);
}

void Shader::use() const {
    if (m_programId != 0) {
        glUseProgram(m_programId);
    }
}

GLint Shader::getUniformLocation(const std::string& name) const {
    auto it = m_uniformLocationCache.find(name);
    if (it != m_uniformLocationCache.end()) {
        return it->second;
    }

    GLint loc = glGetUniformLocation(m_programId, name.c_str());
    m_uniformLocationCache[name] = loc;
    return loc;
}

void Shader::setBool(const std::string& name, bool value) const {
    glUniform1i(getUniformLocation(name), static_cast<int>(value));
}

void Shader::setInt(const std::string& name, int value) const {
    glUniform1i(getUniformLocation(name), value);
}

void Shader::setFloat(const std::string& name, float value) const {
    glUniform1f(getUniformLocation(name), value);
}

void Shader::setVec2(const std::string& name, const glm::vec2& value) const {
    glUniform2fv(getUniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setVec2(const std::string& name, float x, float y) const {
    glUniform2f(getUniformLocation(name), x, y);
}

void Shader::setVec3(const std::string& name, const glm::vec3& value) const {
    glUniform3fv(getUniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setVec3(const std::string& name, float x, float y, float z) const {
    glUniform3f(getUniformLocation(name), x, y, z);
}

void Shader::setVec4(const std::string& name, const glm::vec4& value) const {
    glUniform4fv(getUniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setMat4(const std::string& name, const glm::mat4& mat) const {
    glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(mat));
}

} // namespace vrm
