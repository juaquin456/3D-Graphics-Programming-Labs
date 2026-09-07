//
// Created by juaquin-remon on 9/6/26.
//

#include "common/MeshAsset.h"

#include <algorithm>
#include <glad/glad.h>

MeshAsset::MeshAsset(const std::string &filename): MeshAsset(readPly(filename)) {}

MeshAsset::MeshAsset(const MeshData &data) {
    auto [min_pt, max_pt] = data.bounding_box();
    localCenter = (min_pt + max_pt) * 0.5f;

    glm::vec3 extent = max_pt - min_pt;
    float max_extent = std::max({extent.x, extent.y, extent.z});
    autoScaleFactor = (max_extent > 0.0f) ? (1.0f / max_extent) : 1.0f;
    setupGPU(data);
}

MeshAsset::~MeshAsset() {
    if (m_VAO != 0) {
        glDeleteVertexArrays(1, &m_VAO);
        glDeleteBuffers(1, &m_VBO);
        glDeleteBuffers(1, &m_EBO);
    }
}

void MeshAsset::draw() const {
    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void MeshAsset::setupGPU(const MeshData &data) {
    m_indexCount = static_cast<GLsizei>(data.indices.size());

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, data.vertices.size() * sizeof(float), data.vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices.size() * sizeof(int), data.indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}
