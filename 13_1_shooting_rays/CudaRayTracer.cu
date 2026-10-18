#include "CudaRayTracer.h"

#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include <iostream>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

// -----------------------------------------------------------------
// Möller-Trumbore Ray-Triangle Intersection (Device Function)
// -----------------------------------------------------------------
__device__ inline bool rayTriangleIntersect(
    const glm::vec3& orig,
    const glm::vec3& dir,
    const CudaTriangle& tri,
    float& outT,
    float& outU,
    float& outV
) {
    const float EPSILON = 1e-7f;
    glm::vec3 edge1 = tri.v1 - tri.v0;
    glm::vec3 edge2 = tri.v2 - tri.v0;
    glm::vec3 h = glm::cross(dir, edge2);
    float a = glm::dot(edge1, h);

    // Double-sided test: do not cull backfaces
    if (a > -EPSILON && a < EPSILON) return false;

    float f = 1.0f / a;
    glm::vec3 s = orig - tri.v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = f * glm::dot(edge2, q);
    if (t > EPSILON) {
        outT = t;
        outU = u;
        outV = v;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------
// CUDA Kernel: 1-Depth Ray Tracer with Materials and Direct Lighting
// -----------------------------------------------------------------
__global__ void rayTraceMaterialKernel(
    const CudaTriangle* __restrict__ triangles,
    int numTriangles,
    const CudaMaterial* __restrict__ materials,
    int numMaterials,
    glm::mat4 invProj,
    glm::mat4 invView,
    glm::vec3 camPos,
    CudaLight light,
    int width,
    int height,
    bool enableShadows,
    bool useSmoothNormals,
    int overrideMaterialIndex,
    uchar4* __restrict__ pboPixels
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    // Pixel center in NDC [-1, 1]
    float ndcX = (2.0f * (static_cast<float>(x) + 0.5f)) / static_cast<float>(width) - 1.0f;
    float ndcY = (2.0f * (static_cast<float>(y) + 0.5f)) / static_cast<float>(height) - 1.0f;

    // Unproject from NDC to eye/camera space
    glm::vec4 clipCoords(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 eyeCoords = invProj * clipCoords;
    eyeCoords = glm::vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f);

    // World-space primary ray direction
    glm::vec3 rayDir = glm::normalize(glm::vec3(invView * eyeCoords));

    const float EPSILON = 1e-7f;
    float closestT = 1e30f;
    int hitTriIdx = -1;
    float hitU = 0.0f, hitV = 0.0f;

    // 1. Primary Ray: test against all scene triangles
    for (int t = 0; t < numTriangles; ++t) {
        float dist, u, v;
        if (rayTriangleIntersect(camPos, rayDir, triangles[t], dist, u, v)) {
            if (dist > EPSILON && dist < closestT) {
                closestT = dist;
                hitTriIdx = t;
                hitU = u;
                hitV = v;
            }
        }
    }

    int pixelIdx = y * width + x;

    // If miss, render ambient sky gradient
    if (hitTriIdx == -1) {
        float tSky = 0.5f * (rayDir.y + 1.0f);
        glm::vec3 skyColor = glm::mix(glm::vec3(0.08f, 0.10f, 0.15f), glm::vec3(0.20f, 0.24f, 0.35f), tSky);
        pboPixels[pixelIdx] = make_uchar4(
            static_cast<unsigned char>(glm::clamp(skyColor.r, 0.0f, 1.0f) * 255.0f),
            static_cast<unsigned char>(glm::clamp(skyColor.g, 0.0f, 1.0f) * 255.0f),
            static_cast<unsigned char>(glm::clamp(skyColor.b, 0.0f, 1.0f) * 255.0f),
            255
        );
        return;
    }

    // 2. Hit surface properties
    const CudaTriangle& hitTri = triangles[hitTriIdx];
    glm::vec3 hitPoint = camPos + closestT * rayDir;

    // Normal calculation: Smooth interpolated vertex normal or Flat face normal
    glm::vec3 N;
    if (useSmoothNormals) {
        float w = 1.0f - hitU - hitV;
        N = glm::normalize(w * hitTri.n0 + hitU * hitTri.n1 + hitV * hitTri.n2);
    } else {
        N = glm::normalize(glm::cross(hitTri.v1 - hitTri.v0, hitTri.v2 - hitTri.v0));
    }

    // Ensure normal points towards ray origin (two-sided shading)
    if (glm::dot(N, -rayDir) < 0.0f) {
        N = -N;
    }

    // Retrieve material
    int matId = hitTri.materialId;
    if (overrideMaterialIndex >= 0 && overrideMaterialIndex < numMaterials) {
        matId = overrideMaterialIndex;
    }
    if (matId < 0 || matId >= numMaterials) {
        matId = 0;
    }
    CudaMaterial mat = materials[matId];

    // 3. Direct Lighting Calculation (Blinn-Phong)
    glm::vec3 toLight = light.position - hitPoint;
    float distToLight = glm::length(toLight);
    glm::vec3 L = glm::normalize(toLight);
    glm::vec3 V = glm::normalize(-rayDir);
    glm::vec3 H = glm::normalize(L + V);

    // 4. Shadow Ray (depth 1 occlusion test)
    float shadowFactor = 1.0f;
    if (enableShadows) {
        glm::vec3 shadowOrig = hitPoint + N * 1e-3f; // Normal offset to prevent acne
        for (int s = 0; s < numTriangles; ++s) {
            float sDist, su, sv;
            if (rayTriangleIntersect(shadowOrig, L, triangles[s], sDist, su, sv)) {
                if (sDist > 1e-4f && sDist < distToLight) {
                    shadowFactor = 0.0f; // In shadow
                    break;
                }
            }
        }
    }

    // Ambient term
    glm::vec3 ambient = mat.Ka * light.ambient;

    // Diffuse term (Lambertian)
    float nDotL = fmaxf(glm::dot(N, L), 0.0f);
    glm::vec3 diffuse = mat.Kd * light.diffuse * nDotL;

    // Specular term (Blinn-Phong)
    float nDotH = fmaxf(glm::dot(N, H), 0.0f);
    float specFactor = (nDotL > 0.0f && mat.shininess > 0.0f) ? powf(nDotH, mat.shininess) : 0.0f;
    glm::vec3 specular = mat.Ks * light.specular * specFactor;

    // Combine terms: Ambient + Shadow * (Diffuse + Specular)
    glm::vec3 finalColor = ambient + shadowFactor * (diffuse + specular);
    finalColor = glm::clamp(finalColor, 0.0f, 1.0f);

    // Write RGBA pixel directly into mapped OpenGL PBO in GPU VRAM
    pboPixels[pixelIdx] = make_uchar4(
        static_cast<unsigned char>(finalColor.r * 255.0f),
        static_cast<unsigned char>(finalColor.g * 255.0f),
        static_cast<unsigned char>(finalColor.b * 255.0f),
        255
    );
}

// -----------------------------------------------------------------
// CudaRayTracer Implementation
// -----------------------------------------------------------------
CudaRayTracer::CudaRayTracer() = default;

CudaRayTracer::~CudaRayTracer() {
    cleanup();
}

void CudaRayTracer::cleanup() {
    unregisterPBO();

    if (d_triangles) {
        cudaFree(d_triangles);
        d_triangles = nullptr;
    }
    if (d_materials) {
        cudaFree(d_materials);
        d_materials = nullptr;
    }

    m_triangleCapacity = 0;
    m_triangleCount = 0;
    m_materialCapacity = 0;
    m_materialCount = 0;
}

bool CudaRayTracer::isAvailable() {
    int deviceCount = 0;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);
    return (err == cudaSuccess && deviceCount > 0);
}

std::string CudaRayTracer::getDeviceName() {
    int device = 0;
    if (cudaGetDevice(&device) == cudaSuccess) {
        cudaDeviceProp prop;
        if (cudaGetDeviceProperties(&prop, device) == cudaSuccess) {
            return std::string(prop.name);
        }
    }
    return "Unknown NVIDIA GPU";
}

void CudaRayTracer::uploadScene(const std::vector<CudaTriangle>& triangles, const std::vector<CudaMaterial>& materials) {
    m_triangleCount = triangles.size();
    if (m_triangleCount > 0) {
        if (m_triangleCount > m_triangleCapacity) {
            if (d_triangles) cudaFree(d_triangles);
            cudaError_t err = cudaMalloc(&d_triangles, m_triangleCount * sizeof(CudaTriangle));
            if (err != cudaSuccess) {
                std::cerr << "[CUDA] Error allocating triangles: " << cudaGetErrorString(err) << std::endl;
                m_triangleCapacity = 0;
                return;
            }
            m_triangleCapacity = m_triangleCount;
        }

        cudaError_t err = cudaMemcpy(d_triangles, triangles.data(), m_triangleCount * sizeof(CudaTriangle), cudaMemcpyHostToDevice);
        if (err != cudaSuccess) {
            std::cerr << "[CUDA] Error copying triangles to device: " << cudaGetErrorString(err) << std::endl;
        }
    }

    m_materialCount = materials.size();
    if (m_materialCount > 0) {
        if (m_materialCount > m_materialCapacity) {
            if (d_materials) cudaFree(d_materials);
            cudaError_t err = cudaMalloc(&d_materials, m_materialCount * sizeof(CudaMaterial));
            if (err != cudaSuccess) {
                std::cerr << "[CUDA] Error allocating materials: " << cudaGetErrorString(err) << std::endl;
                m_materialCapacity = 0;
                return;
            }
            m_materialCapacity = m_materialCount;
        }

        cudaError_t err = cudaMemcpy(d_materials, materials.data(), m_materialCount * sizeof(CudaMaterial), cudaMemcpyHostToDevice);
        if (err != cudaSuccess) {
            std::cerr << "[CUDA] Error copying materials to device: " << cudaGetErrorString(err) << std::endl;
        }
    }
}

bool CudaRayTracer::registerPBO(GLuint pbo) {
    unregisterPBO();
    if (pbo == 0) return false;

    cudaError_t err = cudaGraphicsGLRegisterBuffer(&m_cudaPboResource, pbo, cudaGraphicsRegisterFlagsWriteDiscard);
    if (err != cudaSuccess) {
        std::cerr << "[CUDA] Failed to register OpenGL PBO: " << cudaGetErrorString(err) << std::endl;
        m_cudaPboResource = nullptr;
        m_registeredPbo = 0;
        return false;
    }
    m_registeredPbo = pbo;
    return true;
}

void CudaRayTracer::unregisterPBO() {
    if (m_cudaPboResource) {
        cudaGraphicsUnregisterResource(m_cudaPboResource);
        m_cudaPboResource = nullptr;
        m_registeredPbo = 0;
    }
}

bool CudaRayTracer::renderRayTraceToPBO(
    const glm::mat4& invProj,
    const glm::mat4& invView,
    const glm::vec3& camPos,
    const CudaLight& light,
    int width, int height,
    bool enableShadows,
    bool useSmoothNormals,
    int overrideMaterialIndex,
    double& outKernelTimeMs
) {
    if (m_triangleCount == 0 || !d_triangles || !m_cudaPboResource) {
        return false;
    }

    // Map OpenGL PBO resource for direct CUDA write access
    cudaError_t mapErr = cudaGraphicsMapResources(1, &m_cudaPboResource, 0);
    if (mapErr != cudaSuccess) {
        std::cerr << "[CUDA] Error mapping PBO resource: " << cudaGetErrorString(mapErr) << std::endl;
        return false;
    }

    uchar4* d_pboPixels = nullptr;
    size_t mappedSize = 0;
    cudaGraphicsResourceGetMappedPointer((void**)&d_pboPixels, &mappedSize, m_cudaPboResource);

    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);

    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    cudaEventRecord(start);

    // Launch Ray Tracing Kernel with Materials & Direct Lighting
    rayTraceMaterialKernel<<<grid, block>>>(
        d_triangles,
        static_cast<int>(m_triangleCount),
        d_materials,
        static_cast<int>(m_materialCount),
        invProj,
        invView,
        camPos,
        light,
        width,
        height,
        enableShadows,
        useSmoothNormals,
        overrideMaterialIndex,
        d_pboPixels
    );

    cudaEventRecord(stop);
    cudaEventSynchronize(stop);

    // Unmap OpenGL PBO
    cudaGraphicsUnmapResources(1, &m_cudaPboResource, 0);

    float elapsedMs = 0.0f;
    cudaEventElapsedTime(&elapsedMs, start, stop);
    outKernelTimeMs = static_cast<double>(elapsedMs);

    cudaEventDestroy(start);
    cudaEventDestroy(stop);

    return true;
}
