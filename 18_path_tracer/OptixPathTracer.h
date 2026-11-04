#pragma once

#include <optix.h>
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>

#include <string>
#include <vector>
#include <cstdint>

#include "SceneTypes.h"
#include "LaunchParams.h"


struct CUDABuffer {
    void*  ptr         = nullptr;
    size_t sizeInBytes = 0;

    void alloc(size_t size) {
        if (sizeInBytes == size && ptr != nullptr) return;
        free();
        sizeInBytes = size;
        if (sizeInBytes > 0) {
            cudaMalloc(&ptr, sizeInBytes);
        }
    }

    void free() {
        if (ptr) {
            cudaFree(ptr);
            ptr = nullptr;
        }
        sizeInBytes = 0;
    }

    template <typename T>
    void upload(const std::vector<T>& data) {
        alloc(data.size() * sizeof(T));
        if (sizeInBytes > 0) {
            cudaMemcpy(ptr, data.data(), sizeInBytes, cudaMemcpyHostToDevice);
        }
    }
};


template <typename T>
struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) SbtRecord {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
    T    data;
};

struct EmptyData {};

struct HitGroupData {
    GpuTriangle* triangles;
    uint32_t     numTriangles;
    GpuMaterial* materials;
    uint32_t     numMaterials;
};

using RaygenRecord   = SbtRecord<EmptyData>;
using MissRecord     = SbtRecord<EmptyData>;
using HitGroupRecord = SbtRecord<HitGroupData>;


class OptixPathTracer {
public:
    OptixPathTracer() = default;
    ~OptixPathTracer() { cleanup(); }

    static bool isAvailable();
    static std::string deviceName();

    bool init(const std::string& ptxSource);
    void uploadScene(const std::vector<PathTriangle>& triangles,
                     const std::vector<PathMaterial>& materials);

    void resize(uint32_t w, uint32_t h);
    bool registerPBO(GLuint pbo);
    void unregisterPBO();

    double render(const RayGenParams& camera, const PTSettings& settings);

    void resetAccumulation() { m_sampleCount = 0; m_frameIndex = 0; }
    uint32_t sampleCount() const { return m_sampleCount; }

    void cleanup();

private:
    void buildPipeline(const std::string& ptxSource);
    void buildGAS(const std::vector<PathTriangle>& triangles);
    void buildSBT();

    OptixDeviceContext m_ctx    = nullptr;
    cudaStream_t       m_stream = nullptr;

    OptixModule       m_module   = nullptr;
    OptixProgramGroup m_rgPG     = nullptr; // RayGen
    OptixProgramGroup m_missPG   = nullptr; // Miss
    OptixProgramGroup m_hitPG    = nullptr; // HitGroup
    OptixPipeline     m_pipeline = nullptr;

    OptixShaderBindingTable m_sbt         = {};
    OptixTraversableHandle  m_traversable = 0; // Handle del GAS

    CUDABuffer m_gasBuf;
    CUDABuffer m_trianglesBuf;
    CUDABuffer m_materialsBuf;
    CUDABuffer m_launchParamsBuf;
    CUDABuffer m_accumBuf;

    CUDABuffer m_sbtRaygenBuf;
    CUDABuffer m_sbtMissBuf;
    CUDABuffer m_sbtHitBuf;

    GLuint                 m_registeredPbo = 0;
    cudaGraphicsResource_t m_pboResource   = nullptr;

    uint32_t m_width        = 0;
    uint32_t m_height       = 0;
    uint32_t m_sampleCount  = 0;
    uint32_t m_frameIndex   = 0;
    uint32_t m_numTriangles = 0;
    uint32_t m_numMaterials = 0;
};