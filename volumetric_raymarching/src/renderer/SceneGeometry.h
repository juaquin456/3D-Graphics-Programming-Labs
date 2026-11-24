#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

namespace vrm {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
};

class SceneGeometry {
public:
    SceneGeometry();
    ~SceneGeometry();

    void init();
    void renderFloor() const;
    void renderColumns() const;
    void renderBox() const;
    void renderAll() const;

private:
    GLuint m_floorVao;
    GLuint m_floorVbo;
    GLuint m_floorEbo;
    GLsizei m_floorIndexCount;

    GLuint m_columnVao;
    GLuint m_columnVbo;
    GLsizei m_columnIndexCount;

    GLuint m_boxVao;
    GLuint m_boxVbo;
    GLsizei m_boxVertexCount;

    std::vector<glm::mat4> m_columnTransforms;

    void buildFloor();
    void buildColumn();
    void buildBox();
    void cleanup();
};

} // namespace vrm
