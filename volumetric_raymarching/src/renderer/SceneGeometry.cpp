#include "SceneGeometry.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace vrm {

SceneGeometry::SceneGeometry()
    : m_floorVao(0), m_floorVbo(0), m_floorEbo(0), m_floorIndexCount(0),
      m_columnVao(0), m_columnVbo(0), m_columnIndexCount(0),
      m_boxVao(0), m_boxVbo(0), m_boxVertexCount(0)
{}

SceneGeometry::~SceneGeometry() {
    cleanup();
}

void SceneGeometry::cleanup() {
    if (m_floorVao) glDeleteVertexArrays(1, &m_floorVao);
    if (m_floorVbo) glDeleteBuffers(1, &m_floorVbo);
    if (m_floorEbo) glDeleteBuffers(1, &m_floorEbo);
    if (m_columnVao) glDeleteVertexArrays(1, &m_columnVao);
    if (m_columnVbo) glDeleteBuffers(1, &m_columnVbo);
    if (m_boxVao) glDeleteVertexArrays(1, &m_boxVao);
    if (m_boxVbo) glDeleteBuffers(1, &m_boxVbo);
}

void SceneGeometry::init() {
    buildFloor();
    buildColumn();
    buildBox();
}

void SceneGeometry::buildFloor() {
    // Floor quad: size 20x20 at Y = 0 to provide an expansive ground plane
    float size = 10.0f;
    std::vector<Vertex> vertices = {
        { {-size, 0.0f, -size}, {0.0f, 1.0f, 0.0f}, {-size, -size} },
        { { size, 0.0f, -size}, {0.0f, 1.0f, 0.0f}, { size, -size} },
        { { size, 0.0f,  size}, {0.0f, 1.0f, 0.0f}, { size,  size} },
        { {-size, 0.0f,  size}, {0.0f, 1.0f, 0.0f}, {-size,  size} }
    };

    std::vector<GLuint> indices = { 0, 1, 2, 0, 2, 3 };
    m_floorIndexCount = static_cast<GLsizei>(indices.size());

    glGenVertexArrays(1, &m_floorVao);
    glGenBuffers(1, &m_floorVbo);
    glGenBuffers(1, &m_floorEbo);

    glBindVertexArray(m_floorVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_floorVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_floorEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

    // Position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    // Normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    // TexCoords
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords));

    glBindVertexArray(0);
}

void SceneGeometry::buildColumn() {
    // Cylinder column: radius 0.25, height 2.2, 16 segments
    int segments = 16;
    float radius = 0.22f;
    float height = 2.2f;

    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;

    for (int i = 0; i <= segments; ++i) {
        float theta = (2.0f * M_PI * i) / segments;
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);

        glm::vec3 normal(cosT, 0.0f, sinT);

        // Bottom vertex
        vertices.push_back({ {radius * cosT, 0.0f, radius * sinT}, normal, {static_cast<float>(i) / segments, 0.0f} });
        // Top vertex
        vertices.push_back({ {radius * cosT, height, radius * sinT}, normal, {static_cast<float>(i) / segments, 1.0f} });
    }

    for (int i = 0; i < segments; ++i) {
        int i0 = i * 2;
        int i1 = i0 + 1;
        int i2 = (i + 1) * 2;
        int i3 = i2 + 1;

        indices.push_back(i0);
        indices.push_back(i2);
        indices.push_back(i1);

        indices.push_back(i1);
        indices.push_back(i2);
        indices.push_back(i3);
    }

    m_columnIndexCount = static_cast<GLsizei>(indices.size());

    glGenVertexArrays(1, &m_columnVao);
    glGenBuffers(1, &m_columnVbo);
    GLuint ebo;
    glGenBuffers(1, &ebo);

    glBindVertexArray(m_columnVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_columnVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords));

    glBindVertexArray(0);
}

void SceneGeometry::buildBox() {
    // Unit box centered on X/Z [-0.5, 0.5] and resting on Y [0.0, 1.0]
    std::vector<Vertex> vertices = {
        // Front (+Z)
        { {-0.5f, 0.0f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f} },
        { { 0.5f, 0.0f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f} },
        { { 0.5f, 1.0f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f} },
        { {-0.5f, 0.0f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f} },
        { { 0.5f, 1.0f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f} },
        { {-0.5f, 1.0f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 1.0f} },
        // Back (-Z)
        { { 0.5f, 0.0f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f} },
        { {-0.5f, 0.0f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 0.0f} },
        { {-0.5f, 1.0f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f} },
        { { 0.5f, 0.0f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f} },
        { {-0.5f, 1.0f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f} },
        { { 0.5f, 1.0f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 1.0f} },
        // Left (-X)
        { {-0.5f, 0.0f, -0.5f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f} },
        { {-0.5f, 0.0f,  0.5f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f} },
        { {-0.5f, 1.0f,  0.5f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f} },
        { {-0.5f, 0.0f, -0.5f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f} },
        { {-0.5f, 1.0f,  0.5f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f} },
        { {-0.5f, 1.0f, -0.5f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f} },
        // Right (+X)
        { { 0.5f, 0.0f,  0.5f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f} },
        { { 0.5f, 0.0f, -0.5f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f} },
        { { 0.5f, 1.0f, -0.5f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f} },
        { { 0.5f, 0.0f,  0.5f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f} },
        { { 0.5f, 1.0f, -0.5f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f} },
        { { 0.5f, 1.0f,  0.5f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f} },
        // Top (+Y)
        { {-0.5f, 1.0f,  0.5f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 0.0f} },
        { { 0.5f, 1.0f,  0.5f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 0.0f} },
        { { 0.5f, 1.0f, -0.5f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f} },
        { {-0.5f, 1.0f,  0.5f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 0.0f} },
        { { 0.5f, 1.0f, -0.5f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f} },
        { {-0.5f, 1.0f, -0.5f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 1.0f} },
        // Bottom (-Y)
        { {-0.5f, 0.0f, -0.5f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 0.0f} },
        { { 0.5f, 0.0f, -0.5f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 0.0f} },
        { { 0.5f, 0.0f,  0.5f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 1.0f} },
        { {-0.5f, 0.0f, -0.5f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 0.0f} },
        { { 0.5f, 0.0f,  0.5f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 1.0f} },
        { {-0.5f, 0.0f,  0.5f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 1.0f} }
    };

    m_boxVertexCount = static_cast<GLsizei>(vertices.size());

    glGenVertexArrays(1, &m_boxVao);
    glGenBuffers(1, &m_boxVbo);

    glBindVertexArray(m_boxVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_boxVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords));

    glBindVertexArray(0);
}

void SceneGeometry::renderFloor() const {
    glBindVertexArray(m_floorVao);
    glDrawElements(GL_TRIANGLES, m_floorIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void SceneGeometry::renderColumns() const {
    glBindVertexArray(m_columnVao);
    glDrawElements(GL_TRIANGLES, m_columnIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void SceneGeometry::renderBox() const {
    glBindVertexArray(m_boxVao);
    glDrawArrays(GL_TRIANGLES, 0, m_boxVertexCount);
    glBindVertexArray(0);
}

void SceneGeometry::renderAll() const {
    renderFloor();
    renderColumns();
}

} // namespace vrm
