//
// Created by juaquin-remon on 9/6/26.
//

#include "common/MeshAsset.h"

#include <algorithm>
#include <glad/glad.h>


MeshAsset::MeshAsset(const MeshData &data): mesh(std::move(data)) {
    setupGPU();
}

MeshAsset::~MeshAsset() {
    if (m_VAO != 0) {
        glDeleteVertexArrays(1, &m_VAO);
        glDeleteBuffers(1, &m_VBO_Pos);
        if (m_VBO_UV != 0) glDeleteBuffers(1, &m_VBO_UV);
        if (m_VBO_Norm != 0) glDeleteBuffers(1, &m_VBO_Norm);
        if (m_VBO_Tang != 0) glDeleteBuffers(1, &m_VBO_Tang);
        glDeleteBuffers(1, &m_EBO);
    }
}

void MeshAsset::updateGPU() {
   m_indexCount = static_cast<GLsizei>(mesh.indices.size());

    glBindVertexArray(m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO_Pos);
    glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(glm::vec3), mesh.vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    // 2. UVs (location = 1)
    if (!mesh.uvs.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO_UV);
        glBufferData(GL_ARRAY_BUFFER, mesh.uvs.size() * sizeof(glm::vec2), mesh.uvs.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
    }

    // 3. Normales (location = 2)
    if (!mesh.normals.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO_Norm);
        glBufferData(GL_ARRAY_BUFFER, mesh.normals.size() * sizeof(glm::vec3), mesh.normals.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    }

    // 4. Tangentes (location = 3)
    if (!mesh.tangents.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO_Tang);
        glBufferData(GL_ARRAY_BUFFER, mesh.tangents.size() * sizeof(glm::vec3), mesh.tangents.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    }

    // 5. Distancias Geodésicas para Fast Marching (location = 4)
    /* if (!meshData.distances.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO_Dist);
        glBufferData(GL_ARRAY_BUFFER, meshData.distances.size() * sizeof(float), meshData.distances.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    }*/

    // Indices (EBO)
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(int), mesh.indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
}

void MeshAsset::draw() const {
    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void MeshAsset::setupGPU() {
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO_Pos);
    glGenBuffers(1, &m_VBO_UV);
    glGenBuffers(1, &m_VBO_Norm);
    glGenBuffers(1, &m_VBO_Tang);
    glGenBuffers(1, &m_EBO);

    updateGPU();
}
