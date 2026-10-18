#ifndef CUDA_RAY_TRACER_H
#define CUDA_RAY_TRACER_H

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glad/glad.h>

struct CudaTriangle {
    glm::vec3 v0;
    glm::vec3 v1;
    glm::vec3 v2;
};

class CudaRayTracer {
public:
    CudaRayTracer();
    ~CudaRayTracer();

    static bool isAvailable();
    static std::string getDeviceName();

    void uploadTriangles(const std::vector<CudaTriangle>& triangles);

    // Register an OpenGL Pixel Buffer Object (PBO) for zero-copy mapping
    bool registerPBO(GLuint pbo);
    void unregisterPBO();

    // Zero-Copy Render: Cast rays and write normalized grayscale pixels directly into mapped PBO in VRAM
    bool renderDepthToPBO(
        const glm::mat4& invProj,
        const glm::mat4& invView,
        const glm::vec3& camPos,
        const glm::vec3& camFront,
        int width, int height,
        bool usePlanarDepth,
        bool autoRange,
        float manualMin, float manualMax,
        bool invertDepth,
        float& outDetectedMin, float& outDetectedMax,
        int& outHitCount,
        double& outKernelTimeMs
    );

    void cleanup();

private:
    CudaTriangle* d_triangles = nullptr;
    size_t m_triangleCapacity = 0;
    size_t m_triangleCount = 0;

    float* d_depthBuffer = nullptr;
    size_t m_depthBufferCapacity = 0;

    float* d_minMaxHits = nullptr;
    int* d_hitCounter = nullptr;

    struct cudaGraphicsResource* m_cudaPboResource = nullptr;
    GLuint m_registeredPbo = 0;
};

#endif // CUDA_RAY_TRACER_H
