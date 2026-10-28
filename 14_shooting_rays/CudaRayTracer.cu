#include "CudaRayTracer.h"

#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include <iostream>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

// -----------------------------------------------------------------
// Helper Device Atomic Operations for Float Min/Max
// -----------------------------------------------------------------
__device__ inline float atomicMinFloat(float* addr, float val) {
    int* addrAsInt = (int*)addr;
    int old = *addrAsInt, assumed;
    do {
        assumed = old;
        old = atomicCAS(addrAsInt, assumed,
            __float_as_int(fminf(val, __int_as_float(assumed))));
    } while (assumed != old);
    return __int_as_float(old);
}

__device__ inline float atomicMaxFloat(float* addr, float val) {
    int* addrAsInt = (int*)addr;
    int old = *addrAsInt, assumed;
    do {
        assumed = old;
        old = atomicCAS(addrAsInt, assumed,
            __float_as_int(fmaxf(val, __int_as_float(assumed))));
    } while (assumed != old);
    return __int_as_float(old);
}

// -----------------------------------------------------------------
// CUDA Device Kernel 1: Primary Ray Shooting & Depth Calculation
// -----------------------------------------------------------------
__global__ void rayCastDepthKernel(
    const CudaTriangle* __restrict__ triangles,
    int numTriangles,
    glm::mat4 invProj,
    glm::mat4 invView,
    glm::vec3 camPos,
    glm::vec3 camFront,
    int width,
    int height,
    bool usePlanarDepth,
    float* __restrict__ depthBuffer,
    float* __restrict__ minMaxHits,
    int* __restrict__ hitCounter
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    __shared__ float s_min;
    __shared__ float s_max;
    __shared__ int s_hits;

    if (threadIdx.x == 0 && threadIdx.y == 0) {
        s_min = 1e30f;
        s_max = -1e30f;
        s_hits = 0;
    }
    __syncthreads();

    if (x < width && y < height) {
        float ndcX = (2.0f * (static_cast<float>(x) + 0.5f)) / static_cast<float>(width) - 1.0f;
        float ndcY = (2.0f * (static_cast<float>(y) + 0.5f)) / static_cast<float>(height) - 1.0f;

        glm::vec4 clipCoords(ndcX, ndcY, -1.0f, 1.0f);
        glm::vec4 eyeCoords = invProj * clipCoords;
        eyeCoords = glm::vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f);

        glm::vec3 rayDir = glm::normalize(glm::vec3(invView * eyeCoords));

        const float EPSILON = 1e-7f;
        float closestT = 1e30f;

        for (int t = 0; t < numTriangles; ++t) {
            glm::vec3 v0 = triangles[t].v0;
            glm::vec3 v1 = triangles[t].v1;
            glm::vec3 v2 = triangles[t].v2;

            glm::vec3 edge1 = v1 - v0;
            glm::vec3 edge2 = v2 - v0;
            glm::vec3 h = glm::cross(rayDir, edge2);
            float a = glm::dot(edge1, h);

            if (a > -EPSILON && a < EPSILON) continue;

            float f = 1.0f / a;
            glm::vec3 s = camPos - v0;
            float u = f * glm::dot(s, h);
            if (u < 0.0f || u > 1.0f) continue;

            glm::vec3 q = glm::cross(s, edge1);
            float v = f * glm::dot(rayDir, q);
            if (v < 0.0f || u + v > 1.0f) continue;

            float dist = f * glm::dot(edge2, q);
            if (dist > EPSILON && dist < closestT) {
                closestT = dist;
            }
        }

        int idx = y * width + x;
        if (closestT < 1e29f) {
            float depthVal = usePlanarDepth ? (closestT * glm::dot(rayDir, camFront)) : closestT;
            depthBuffer[idx] = depthVal;

            atomicMinFloat(&s_min, depthVal);
            atomicMaxFloat(&s_max, depthVal);
            atomicAdd(&s_hits, 1);
        } else {
            depthBuffer[idx] = 1e30f;
        }
    }
    __syncthreads();

    if (threadIdx.x == 0 && threadIdx.y == 0) {
        if (s_min < 1e29f) {
            atomicMinFloat(&minMaxHits[0], s_min);
        }
        if (s_max > -1e29f) {
            atomicMaxFloat(&minMaxHits[1], s_max);
        }
        if (s_hits > 0) {
            atomicAdd(hitCounter, s_hits);
        }
    }
}

// -----------------------------------------------------------------
// CUDA Device Kernel 2: Normalize Depth to [0:1] & Write to OpenGL PBO
// -----------------------------------------------------------------
__global__ void normalizeDepthToPboKernel(
    const float* __restrict__ depthBuffer,
    float effMin,
    float effMax,
    bool invertDepth,
    int width,
    int height,
    uchar4* __restrict__ pboPixels
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    int idx = y * width + x;
    float d = depthBuffer[idx];
    float intensity = 0.0f;

    if (d >= 1e29f) {
        intensity = invertDepth ? 0.0f : 1.0f;
    } else {
        float norm = (d - effMin) / (effMax - effMin);
        norm = fminf(fmaxf(norm, 0.0f), 1.0f);
        intensity = invertDepth ? (1.0f - norm) : norm;
    }

    unsigned char byteVal = static_cast<unsigned char>(fminf(fmaxf(intensity * 255.0f, 0.0f), 255.0f));
    pboPixels[idx] = make_uchar4(byteVal, byteVal, byteVal, 255);
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
    if (d_depthBuffer) {
        cudaFree(d_depthBuffer);
        d_depthBuffer = nullptr;
    }
    if (d_minMaxHits) {
        cudaFree(d_minMaxHits);
        d_minMaxHits = nullptr;
    }
    if (d_hitCounter) {
        cudaFree(d_hitCounter);
        d_hitCounter = nullptr;
    }

    m_triangleCapacity = 0;
    m_triangleCount = 0;
    m_depthBufferCapacity = 0;
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

void CudaRayTracer::uploadTriangles(const std::vector<CudaTriangle>& triangles) {
    m_triangleCount = triangles.size();
    if (m_triangleCount == 0) return;

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

bool CudaRayTracer::renderDepthToPBO(
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
) {
    if (m_triangleCount == 0 || !d_triangles || !m_cudaPboResource) {
        return false;
    }

    size_t totalPixels = static_cast<size_t>(width) * height;

    if (totalPixels > m_depthBufferCapacity) {
        if (d_depthBuffer) cudaFree(d_depthBuffer);
        cudaError_t err = cudaMalloc(&d_depthBuffer, totalPixels * sizeof(float));
        if (err != cudaSuccess) {
            std::cerr << "[CUDA] Error allocating depth buffer: " << cudaGetErrorString(err) << std::endl;
            m_depthBufferCapacity = 0;
            return false;
        }
        m_depthBufferCapacity = totalPixels;
    }

    if (!d_minMaxHits) {
        cudaMalloc(&d_minMaxHits, 2 * sizeof(float));
    }
    if (!d_hitCounter) {
        cudaMalloc(&d_hitCounter, sizeof(int));
    }

    // Initialize min/max and hit counter on device
    float initMinMax[2] = { 1e30f, -1e30f };
    int initHits = 0;
    cudaMemcpy(d_minMaxHits, initMinMax, 2 * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(d_hitCounter, &initHits, sizeof(int), cudaMemcpyHostToDevice);

    // Map OpenGL PBO for CUDA direct write access
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

    // Pass 1: Cast primary rays and record raw depths
    rayCastDepthKernel<<<grid, block>>>(
        d_triangles,
        static_cast<int>(m_triangleCount),
        invProj,
        invView,
        camPos,
        camFront,
        width,
        height,
        usePlanarDepth,
        d_depthBuffer,
        d_minMaxHits,
        d_hitCounter
    );

    // Retrieve detected depth range and hit counter (only 12 bytes copied to host)
    float detectedMinMax[2];
    int hostHits = 0;
    cudaMemcpy(detectedMinMax, d_minMaxHits, 2 * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(&hostHits, d_hitCounter, sizeof(int), cudaMemcpyDeviceToHost);

    if (hostHits == 0) {
        detectedMinMax[0] = 0.0f;
        detectedMinMax[1] = 1.0f;
    }
    outDetectedMin = detectedMinMax[0];
    outDetectedMax = detectedMinMax[1];
    outHitCount = hostHits;

    float effMin = autoRange ? detectedMinMax[0] : manualMin;
    float effMax = autoRange ? detectedMinMax[1] : manualMax;
    if (effMax - effMin < 1e-6f) {
        effMax = effMin + 1.0f;
    }

    // Pass 2: Normalize depth and write directly into mapped OpenGL PBO (in VRAM)
    normalizeDepthToPboKernel<<<grid, block>>>(
        d_depthBuffer,
        effMin,
        effMax,
        invertDepth,
        width,
        height,
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
