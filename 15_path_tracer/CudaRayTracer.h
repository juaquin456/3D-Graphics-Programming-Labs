#ifndef CUDA_RAY_TRACER_H
#define CUDA_RAY_TRACER_H

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glad/glad.h>

// -------------------------------------------------------------
// Material representation on GPU
// -------------------------------------------------------------
struct CudaMaterial {
    glm::vec3 Ka{0.2f};
    glm::vec3 Kd{0.8f, 0.8f, 0.8f};
    glm::vec3 Ks{1.0f};
    float shininess{32.0f};
};

// -------------------------------------------------------------
// World-space triangle with vertex normals and material ID
// -------------------------------------------------------------
struct CudaTriangle {
    glm::vec3 v0, v1, v2;    // World-space vertex positions
    glm::vec3 n0, n1, n2;    // World-space vertex normals
    int materialId{0};        // Index into materials array
};

// -------------------------------------------------------------
// Light representation on GPU
// -------------------------------------------------------------
struct CudaLight {
    glm::vec3 position{2.0f, 6.0f, 3.0f};
    glm::vec3 ambient{0.25f};
    glm::vec3 diffuse{0.85f};
    glm::vec3 specular{1.0f};
};

// -------------------------------------------------------------
// CUDA Ray Tracer (Diffuse Surfaces + Shadows + Zero-Copy PBO)
// -------------------------------------------------------------
class CudaRayTracer {
public:
    CudaRayTracer();
    ~CudaRayTracer();

    static bool isAvailable();
    static std::string getDeviceName();

    // Upload scene triangles and material definitions to GPU memory
    void uploadScene(const std::vector<CudaTriangle>& triangles, const std::vector<CudaMaterial>& materials);

    // Register / unregister OpenGL PBO for zero-copy DMA transfer
    bool registerPBO(GLuint pbo);
    void unregisterPBO();

    // Ray Tracer: Primary rays + Diffuse Materials + Direct Shadows
    bool renderRayTraceToPBO(
        const glm::mat4& invProj,
        const glm::mat4& invView,
        const glm::vec3& camPos,
        const CudaLight& light,
        int width, int height,
        bool enableShadows,
        bool useSmoothNormals,
        int overrideMaterialIndex, // -1: use scene material, >= 0: preset override
        bool diffuseOnly,
        double& outKernelTimeSec
    );

    void cleanup();

private:
    CudaTriangle* d_triangles = nullptr;
    size_t m_triangleCapacity = 0;
    size_t m_triangleCount = 0;

    CudaMaterial* d_materials = nullptr;
    size_t m_materialCapacity = 0;
    size_t m_materialCount = 0;

    struct cudaGraphicsResource* m_cudaPboResource = nullptr;
    GLuint m_registeredPbo = 0;
};

#endif // CUDA_RAY_TRACER_H
