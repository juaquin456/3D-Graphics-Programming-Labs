//
// Created by juaquin-remon on 9/6/26.
//

#ifndef ANIMATION_MESHASSET_H
#define ANIMATION_MESHASSET_H
#include <memory>

#include "MeshData.h"
#include "glad/glad.h"


class MeshAsset {
public:
    glm::vec3 localCenter{0};
    float autoScaleFactor{1};

    using Ptr = std::shared_ptr<MeshAsset>;

    explicit MeshAsset(const std::string& filename);

    explicit MeshAsset(const MeshData& data);

    ~MeshAsset();

    MeshAsset(const MeshAsset&) = delete;
    MeshAsset& operator=(const MeshAsset&) = delete;

    void draw() const;

private:
    unsigned int m_VAO{0}, m_VBO{0}, m_EBO{0};
    GLsizei m_indexCount{0};

    void setupGPU(const MeshData& data);
};


#endif //ANIMATION_MESHASSET_H
