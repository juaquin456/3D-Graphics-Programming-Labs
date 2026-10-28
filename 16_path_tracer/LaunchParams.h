#pragma once

#include <optix.h>
#include <vector_types.h>

struct GpuMaterial {
    unsigned int type; // 0: Diffuse, 1: Specular, 2: Dielectric
    float3       albedo;
    float3       emission;
    float        ior;
    float        roughness;
};

struct GpuTriangle {
    float3       v0, v1, v2;
    float3       n0, n1, n2;
    unsigned int materialId;
};

struct GpuCamera {
    float3       origin;
    float3       lowerLeftCorner;
    float3       horizontal;
    float3       vertical;
    unsigned int frameIndex;
};

struct LaunchParams {
    float*                 accumBuffer;
    uchar4*                frameBuffer;
    unsigned int           width;
    unsigned int           height;
    unsigned int           frameIndex;
    unsigned int           sampleCount;
    int                    maxDepth;

    OptixTraversableHandle traversable;

    GpuCamera              camera;
    GpuTriangle*           triangles;
    unsigned int           numTriangles;
    GpuMaterial*           materials;
    unsigned int           numMaterials;
};