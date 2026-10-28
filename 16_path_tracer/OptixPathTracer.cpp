/**
 * OptixPathTracer.cpp – Fully Decoupled OptiX 7/8 GPU Path Tracer Implementation
 */

#include "OptixPathTracer.h"

#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#include <optix_stack_size.h>

#include <iostream>
#include <stdexcept>
#include <cstring>


#define CUDA_CHECK(call)                                                      \
    do {                                                                      \
        cudaError_t error = call;                                             \
        if (error != cudaSuccess) {                                           \
            std::cerr << "[CUDA Error] " << cudaGetErrorString(error)         \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl;  \
        }                                                                     \
    } while (0)

#define OPTIX_CHECK(call)                                                     \
    do {                                                                      \
        OptixResult res = call;                                               \
        if (res != OPTIX_SUCCESS) {                                           \
            std::cerr << "[OptiX Error] Code " << res                         \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl;  \
        }                                                                     \
    } while (0)

static void optixLogCallback(unsigned int level, const char* tag, const char* message, void*) {
    if (level <= 2) {
        std::cerr << "[OptiX][" << tag << "] " << message << std::endl;
    }
}


static inline float3 toFloat3(const Vec3& v) {
    return make_float3(v.x, v.y, v.z);
}



bool OptixPathTracer::isAvailable() {
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess || count == 0) return false;
    return optixInit() == OPTIX_SUCCESS;
}

std::string OptixPathTracer::deviceName() {
    cudaDeviceProp prop;
    if (cudaGetDeviceProperties(&prop, 0) == cudaSuccess) {
        return prop.name;
    }
    return "Unknown NVIDIA GPU";
}


bool OptixPathTracer::init(const std::string& ptxSource) {
    if (ptxSource.empty()) return false;

    CUDA_CHECK(cudaFree(0)); // Force CUDA context initialization
    OPTIX_CHECK(optixInit());

    CUDA_CHECK(cudaStreamCreate(&m_stream));

    OptixDeviceContextOptions options = {};
    options.logCallbackFunction = optixLogCallback;
    options.logCallbackLevel    = 3;

    CUcontext cuCtx = nullptr; // Use active CUDA context
    OPTIX_CHECK(optixDeviceContextCreate(cuCtx, &options, &m_ctx));

    buildPipeline(ptxSource);
    return true;
}

void OptixPathTracer::cleanup() {
    if (m_stream) {
        CUDA_CHECK(cudaStreamSynchronize(m_stream));
    }

    unregisterPBO();

    if (m_pipeline) { optixPipelineDestroy(m_pipeline); m_pipeline = nullptr; }
    if (m_rgPG)     { optixProgramGroupDestroy(m_rgPG); m_rgPG = nullptr; }
    if (m_missPG)   { optixProgramGroupDestroy(m_missPG); m_missPG = nullptr; }
    if (m_hitPG)    { optixProgramGroupDestroy(m_hitPG); m_hitPG = nullptr; }
    if (m_module)   { optixModuleDestroy(m_module); m_module = nullptr; }
    if (m_ctx)      { optixDeviceContextDestroy(m_ctx); m_ctx = nullptr; }

    m_gasBuf.free();
    m_trianglesBuf.free();
    m_materialsBuf.free();
    m_launchParamsBuf.free();
    m_accumBuf.free();

    m_sbtRaygenBuf.free();
    m_sbtMissBuf.free();
    m_sbtHitBuf.free();

    if (m_stream) {
        cudaStreamDestroy(m_stream);
        m_stream = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────
//  Pipeline Compilation & OptiX Shader Linkage
// ─────────────────────────────────────────────────────────────
void OptixPathTracer::buildPipeline(const std::string& ptxSource) {
    OptixModuleCompileOptions moduleCompileOptions = {};
    moduleCompileOptions.maxRegisterCount = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
    moduleCompileOptions.optLevel         = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    moduleCompileOptions.debugLevel       = OPTIX_COMPILE_DEBUG_LEVEL_NONE;

    OptixPipelineCompileOptions pipelineCompileOptions = {};
    pipelineCompileOptions.usesMotionBlur                   = 0;
    pipelineCompileOptions.traversableGraphFlags           = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_GAS;
    pipelineCompileOptions.numPayloadValues                = 2;
    pipelineCompileOptions.numAttributeValues              = 2;
    pipelineCompileOptions.exceptionFlags                  = OPTIX_EXCEPTION_FLAG_NONE;
    pipelineCompileOptions.pipelineLaunchParamsVariableName = "g_params";

    char log[2048];
    size_t logSz = sizeof(log);

    // 1. Create PTX Module
    OPTIX_CHECK(optixModuleCreate(
        m_ctx, &moduleCompileOptions, &pipelineCompileOptions,
        ptxSource.c_str(), ptxSource.size(),
        log, &logSz, &m_module
    ));

    // 2. Program Groups (RayGen, Miss, ClosestHit)
    OptixProgramGroupOptions pgOpts = {};

    // RayGen
    OptixProgramGroupDesc rgDesc = {};
    rgDesc.kind                     = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    rgDesc.raygen.module            = m_module;
    rgDesc.raygen.entryFunctionName = "__raygen__pt";
    logSz = sizeof(log);
    OPTIX_CHECK(optixProgramGroupCreate(m_ctx, &rgDesc, 1, &pgOpts, log, &logSz, &m_rgPG));

    // Miss
    OptixProgramGroupDesc missDesc = {};
    missDesc.kind                   = OPTIX_PROGRAM_GROUP_KIND_MISS;
    missDesc.miss.module            = m_module;
    missDesc.miss.entryFunctionName = "__miss__sky";
    logSz = sizeof(log);
    OPTIX_CHECK(optixProgramGroupCreate(m_ctx, &missDesc, 1, &pgOpts, log, &logSz, &m_missPG));

    // Hit Group (Closest Hit)
    OptixProgramGroupDesc hitDesc = {};
    hitDesc.kind                         = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    hitDesc.hitgroup.moduleCH            = m_module;
    hitDesc.hitgroup.entryFunctionNameCH = "__closesthit__pt";
    logSz = sizeof(log);
    OPTIX_CHECK(optixProgramGroupCreate(m_ctx, &hitDesc, 1, &pgOpts, log, &logSz, &m_hitPG));

    // 3. Link Program Groups into an Executable Pipeline
    OptixProgramGroup programGroups[] = { m_rgPG, m_missPG, m_hitPG };
    OptixPipelineLinkOptions linkOpts = {};
    linkOpts.maxTraceDepth = 2;

    logSz = sizeof(log);
    OPTIX_CHECK(optixPipelineCreate(
        m_ctx, &pipelineCompileOptions, &linkOpts,
        programGroups, 3, log, &logSz, &m_pipeline
    ));

    // 4. Configure Pipeline Stack Memory
    OptixStackSizes stackSizes = {};
    for (auto pg : programGroups) {
        OPTIX_CHECK(optixUtilAccumulateStackSizes(pg, &stackSizes, m_pipeline));
    }
    uint32_t directCallableStackSizeFromTraversal;
    uint32_t directCallableStackSizeFromState;
    uint32_t continuationStackSize;
    OPTIX_CHECK(optixUtilComputeStackSizes(
        &stackSizes, 2, 0, 0,
        &directCallableStackSizeFromTraversal,
        &directCallableStackSizeFromState,
        &continuationStackSize
    ));
    OPTIX_CHECK(optixPipelineSetStackSize(
        m_pipeline,
        directCallableStackSizeFromTraversal,
        directCallableStackSizeFromState,
        continuationStackSize,
        1
    ));
}


void OptixPathTracer::uploadScene(const std::vector<PathTriangle>& triangles,
                                   const std::vector<PathMaterial>& materials) {
    resetAccumulation();

    if (triangles.empty()) {
        m_numTriangles = 0;
        m_numMaterials = 0;
        return;
    }

    std::vector<GpuTriangle> gpuTris(triangles.size());
    for (size_t i = 0; i < triangles.size(); ++i) {
        gpuTris[i].v0         = toFloat3(triangles[i].v0);
        gpuTris[i].v1         = toFloat3(triangles[i].v1);
        gpuTris[i].v2         = toFloat3(triangles[i].v2);
        gpuTris[i].n0         = toFloat3(triangles[i].n0);
        gpuTris[i].n1         = toFloat3(triangles[i].n1);
        gpuTris[i].n2         = toFloat3(triangles[i].n2);
        gpuTris[i].materialId = triangles[i].materialId;
    }

    std::vector<GpuMaterial> gpuMats(materials.size());
    for (size_t i = 0; i < materials.size(); ++i) {
        gpuMats[i].type      = static_cast<uint32_t>(materials[i].type);
        gpuMats[i].albedo    = toFloat3(materials[i].albedo);
        gpuMats[i].emission  = toFloat3(materials[i].emission);
        gpuMats[i].ior       = materials[i].ior;
        gpuMats[i].roughness = materials[i].roughness;
    }

    m_trianglesBuf.upload(gpuTris);
    m_materialsBuf.upload(gpuMats);

    m_numTriangles = static_cast<uint32_t>(gpuTris.size());
    m_numMaterials = static_cast<uint32_t>(gpuMats.size());

    buildGAS(triangles);
    buildSBT();
}

void OptixPathTracer::buildGAS(const std::vector<PathTriangle>& triangles) {
    std::vector<float3> vertices;
    vertices.reserve(triangles.size() * 3);
    for (const auto& tri : triangles) {
        vertices.push_back(toFloat3(tri.v0));
        vertices.push_back(toFloat3(tri.v1));
        vertices.push_back(toFloat3(tri.v2));
    }

    CUDABuffer d_verts;
    d_verts.upload(vertices);

    uint32_t triangleInputFlags[1] = { OPTIX_GEOMETRY_FLAG_NONE };

    OptixBuildInput triangleInput = {};
    triangleInput.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;

    CUdeviceptr d_vptr = reinterpret_cast<CUdeviceptr>(d_verts.ptr);
    triangleInput.triangleArray.vertexBuffers       = &d_vptr;
    triangleInput.triangleArray.numVertices         = static_cast<uint32_t>(vertices.size());
    triangleInput.triangleArray.vertexFormat       = OPTIX_VERTEX_FORMAT_FLOAT3;
    triangleInput.triangleArray.vertexStrideInBytes = sizeof(float3);
    triangleInput.triangleArray.flags               = triangleInputFlags;
    triangleInput.triangleArray.numSbtRecords       = 1;

    OptixAccelBuildOptions accelOpts = {};
    accelOpts.buildFlags            = OPTIX_BUILD_FLAG_ALLOW_COMPACTION | OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    accelOpts.operation             = OPTIX_BUILD_OPERATION_BUILD;

    OptixAccelBufferSizes accelSizes;
    OPTIX_CHECK(optixAccelComputeMemoryUsage(m_ctx, &accelOpts, &triangleInput, 1, &accelSizes));

    CUDABuffer d_temp;
    d_temp.alloc(accelSizes.tempSizeInBytes);

    CUDABuffer d_output;
    d_output.alloc(accelSizes.outputSizeInBytes);

    OPTIX_CHECK(optixAccelBuild(
        m_ctx, m_stream,
        &accelOpts, &triangleInput, 1,
        reinterpret_cast<CUdeviceptr>(d_temp.ptr), d_temp.sizeInBytes,
        reinterpret_cast<CUdeviceptr>(d_output.ptr), d_output.sizeInBytes,
        &m_traversable, nullptr, 0
    ));

    CUDA_CHECK(cudaStreamSynchronize(m_stream));
    d_temp.free();
    d_verts.free();

    m_gasBuf = std::move(d_output);
}


void OptixPathTracer::buildSBT() {
    RaygenRecord rgRec = {};
    OPTIX_CHECK(optixSbtRecordPackHeader(m_rgPG, &rgRec));
    m_sbtRaygenBuf.upload(std::vector<RaygenRecord>{ rgRec });

    MissRecord missRec = {};
    OPTIX_CHECK(optixSbtRecordPackHeader(m_missPG, &missRec));
    m_sbtMissBuf.upload(std::vector<MissRecord>{ missRec });

    HitGroupRecord hitRec = {};
    OPTIX_CHECK(optixSbtRecordPackHeader(m_hitPG, &hitRec));
    hitRec.data.triangles    = reinterpret_cast<GpuTriangle*>(m_trianglesBuf.ptr);
    hitRec.data.numTriangles = m_numTriangles;
    hitRec.data.materials    = reinterpret_cast<GpuMaterial*>(m_materialsBuf.ptr);
    hitRec.data.numMaterials = m_numMaterials;
    m_sbtHitBuf.upload(std::vector<HitGroupRecord>{ hitRec });

    m_sbt.raygenRecord                = reinterpret_cast<CUdeviceptr>(m_sbtRaygenBuf.ptr);
    m_sbt.missRecordBase              = reinterpret_cast<CUdeviceptr>(m_sbtMissBuf.ptr);
    m_sbt.missRecordStrideInBytes     = sizeof(MissRecord);
    m_sbt.missRecordCount             = 1;
    m_sbt.hitgroupRecordBase          = reinterpret_cast<CUdeviceptr>(m_sbtHitBuf.ptr);
    m_sbt.hitgroupRecordStrideInBytes = sizeof(HitGroupRecord);
    m_sbt.hitgroupRecordCount         = 1;
}

bool OptixPathTracer::registerPBO(GLuint pbo) {
    if (m_registeredPbo == pbo && m_pboResource != nullptr) return true;
    unregisterPBO();
    m_registeredPbo = pbo;
    if (pbo == 0) return false;

    CUDA_CHECK(cudaGraphicsGLRegisterBuffer(
        &m_pboResource, m_registeredPbo, cudaGraphicsRegisterFlagsWriteDiscard
    ));
    return m_pboResource != nullptr;
}

void OptixPathTracer::unregisterPBO() {
    if (m_pboResource) {
        cudaGraphicsUnregisterResource(m_pboResource);
        m_pboResource   = nullptr;
        m_registeredPbo = 0;
    }
}

void OptixPathTracer::resize(uint32_t w, uint32_t h) {
    if (m_width == w && m_height == h) return;
    m_width  = w;
    m_height = h;
    m_accumBuf.alloc(m_width * m_height * 4 * sizeof(float));
    resetAccumulation();
}


double OptixPathTracer::render(const RayGenParams& camera, const PTSettings& settings) {
    if (!m_traversable || !m_pboResource || m_width == 0 || m_numTriangles == 0) return 0.0;

    uchar4* d_framebuffer = nullptr;
    size_t sz = 0;
    CUDA_CHECK(cudaGraphicsMapResources(1, &m_pboResource, m_stream));
    CUDA_CHECK(cudaGraphicsResourceGetMappedPointer((void**)&d_framebuffer, &sz, m_pboResource));

    if (!d_framebuffer) {
        cudaGraphicsUnmapResources(1, &m_pboResource, m_stream);
        return 0.0;
    }

    GpuCamera gpuCam;
    gpuCam.origin          = toFloat3(camera.origin);
    gpuCam.lowerLeftCorner = toFloat3(camera.lowerLeftCorner);
    gpuCam.horizontal      = toFloat3(camera.horizontal);
    gpuCam.vertical        = toFloat3(camera.vertical);
    gpuCam.frameIndex      = camera.frameIndex;

    LaunchParams params = {};
    params.accumBuffer  = reinterpret_cast<float*>(m_accumBuf.ptr);
    params.frameBuffer  = d_framebuffer;
    params.width        = m_width;
    params.height       = m_height;
    params.frameIndex   = m_frameIndex++;
    params.sampleCount  = m_sampleCount;
    params.camera       = gpuCam;
    params.maxDepth     = settings.maxDepth;
    params.traversable  = m_traversable;
    params.triangles    = reinterpret_cast<GpuTriangle*>(m_trianglesBuf.ptr);
    params.numTriangles = m_numTriangles;
    params.materials    = reinterpret_cast<GpuMaterial*>(m_materialsBuf.ptr);
    params.numMaterials = m_numMaterials;

    if (m_launchParamsBuf.sizeInBytes < sizeof(LaunchParams)) {
        m_launchParamsBuf.alloc(sizeof(LaunchParams));
    }

    CUDA_CHECK(cudaMemcpyAsync(
        m_launchParamsBuf.ptr, &params, sizeof(LaunchParams),
        cudaMemcpyHostToDevice, m_stream
    ));

    cudaEvent_t t0, t1;
    CUDA_CHECK(cudaEventCreate(&t0));
    CUDA_CHECK(cudaEventCreate(&t1));
    CUDA_CHECK(cudaEventRecord(t0, m_stream));

    OPTIX_CHECK(optixLaunch(
        m_pipeline, m_stream,
        reinterpret_cast<CUdeviceptr>(m_launchParamsBuf.ptr), sizeof(LaunchParams),
        &m_sbt,
        m_width, m_height, 1
    ));

    CUDA_CHECK(cudaEventRecord(t1, m_stream));
    CUDA_CHECK(cudaEventSynchronize(t1));

    float renderTimeMs = 0.0f;
    CUDA_CHECK(cudaEventElapsedTime(&renderTimeMs, t0, t1));
    CUDA_CHECK(cudaEventDestroy(t0));
    CUDA_CHECK(cudaEventDestroy(t1));

    // Unmap OpenGL PBO to allow OpenGL to sample the texture
    CUDA_CHECK(cudaGraphicsUnmapResources(1, &m_pboResource, m_stream));

    m_sampleCount++;
    return static_cast<double>(renderTimeMs);
}