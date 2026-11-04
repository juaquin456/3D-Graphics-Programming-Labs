#include <optix.h>
#include <cuda_runtime.h>
#include "LaunchParams.h"
#include <cstdint>
extern "C" {
    __constant__ LaunchParams g_params;
}


struct PCG32Device {
    uint64_t state, inc;

    __device__ void seed(uint64_t s, uint64_t seq) {
        state = 0;
        inc   = (seq << 1) | 1u;
        next(); state += s; next();
    }
    __device__ uint32_t next() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        uint32_t xs  = ((old >> 18u) ^ old) >> 27u;
        uint32_t rot = old >> 59u;
        return (xs >> rot) | (xs << ((-rot) & 31));
    }
    __device__ float nextFloat() {
        return (next() >> 8) * (1.0f / 16777216.0f);
    }
};


__device__ inline float3 operator+(const float3& a, const float3& b) { return make_float3(a.x+b.x, a.y+b.y, a.z+b.z); }
__device__ inline float3 operator-(const float3& a, const float3& b) { return make_float3(a.x-b.x, a.y-b.y, a.z-b.z); }
__device__ inline float3 operator*(const float3& a, float s)         { return make_float3(a.x*s, a.y*s, a.z*s); }
__device__ inline float3 operator*(float s, const float3& a)         { return make_float3(a.x*s, a.y*s, a.z*s); }
__device__ inline float3 operator*(const float3& a, const float3& b) { return make_float3(a.x*b.x, a.y*b.y, a.z*b.z); }
__device__ inline float3 operator/(const float3& a, float s)         { float inv = 1.0f/s; return make_float3(a.x*inv, a.y*inv, a.z*inv); }
__device__ inline float3 operator-(const float3& a)                  { return make_float3(-a.x, -a.y, -a.z); }

__device__ inline float dot(const float3& a, const float3& b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

__device__ inline float3 cross(const float3& a, const float3& b) {
    return make_float3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

__device__ inline float length(const float3& v) {
    return sqrtf(dot(v, v));
}

__device__ inline float3 normalize(const float3& v) {
    float len = length(v);
    return (len > 0.0f) ? (v / len) : make_float3(0.0f, 0.0f, 0.0f);
}

__device__ inline float3 reflect(const float3& I, const float3& N) {
    return I - 2.0f * dot(I, N) * N;
}

__device__ inline bool refract(const float3& I, const float3& N, float ratio, float3& refracted) {
    float3 uI = normalize(I);
    float dt  = dot(uI, N);
    float disc = 1.0f - ratio * ratio * (1.0f - dt * dt);
    if (disc <= 0.0f) return false; // Total Internal Reflection
    refracted = normalize(ratio * (uI - N * dt) - N * sqrtf(disc));
    return true;
}

__device__ inline float schlick(float cosine, float ior) {
    float r0 = (1.0f - ior) / (1.0f + ior);
    r0 *= r0;
    return r0 + (1.0f - r0) * powf(1.0f - cosine, 5.0f);
}

__device__ inline float3 cosineSampleHemisphere(float r1, float r2) {
    float phi = 6.28318530718f * r1;
    float st  = sqrtf(r2);
    return make_float3(cosf(phi) * st, sinf(phi) * st, sqrtf(1.0f - r2));
}

__device__ inline float3 toWorld(const float3& s, const float3& N) {
    float3 up = (fabsf(N.x) < 0.999f) ? make_float3(1.0f, 0.0f, 0.0f) : make_float3(0.0f, 1.0f, 0.0f);
    float3 T  = normalize(cross(up, N));
    float3 B  = cross(N, T);
    return normalize(s.x * T + s.y * B + s.z * N);
}

__device__ inline float3 missColor(const float3& dir) {
    float t = 0.5f * (normalize(dir).y + 1.0f);
    return (1.0f - t) * make_float3(1.0f, 1.0f, 1.0f) + t * make_float3(0.5f, 0.7f, 1.0f);
}

__device__ inline unsigned char gammaEncode(float v) {
    float g = powf(fmaxf(0.0f, fminf(v, 1.0f)), 1.0f / 2.2f);
    return (unsigned char)(g * 255.99f);
}


struct PRD {
    float3      emission;
    float3      attenuation;
    float3      nextOrigin;
    float3      nextDirection;
    bool        hit;
    PCG32Device rng;
};

__device__ inline PRD* getPRD() {
    const uint32_t u0 = optixGetPayload_0();
    const uint32_t u1 = optixGetPayload_1();
    return reinterpret_cast<PRD*>(static_cast<uint64_t>(u0) | (static_cast<uint64_t>(u1) << 32));
}

__device__ inline void packPointer(PRD* ptr, uint32_t& u0, uint32_t& u1) {
    uint64_t val = reinterpret_cast<uint64_t>(ptr);
    u0 = static_cast<uint32_t>(val & 0xFFFFFFFF);
    u1 = static_cast<uint32_t>(val >> 32);
}

// --- Pixel Filters on GPU ---
__device__ inline float evalBox1DGpu(float x, float radius) {
    return (fabsf(x) <= radius) ? 1.0f : 0.0f;
}

__device__ inline float evalTriangle1DGpu(float x, float radius) {
    float d = fabsf(x);
    if (d > radius || radius <= 1e-6f) return 0.0f;
    return 1.0f - d / radius;
}

__device__ inline float evalGaussian1DGpu(float x, float radius, float alpha) {
    float d = fabsf(x);
    if (d > radius) return 0.0f;
    float e = expf(-alpha * d * d);
    float eEdge = expf(-alpha * radius * radius);
    return fmaxf(0.0f, e - eEdge);
}

__device__ inline float evalMitchellNetravali1DGpu(float x, float radius, float B, float C) {
    if (radius <= 1e-6f) return 0.0f;
    float d = fabsf(x) * (2.0f / radius);
    if (d >= 2.0f) return 0.0f;

    float d2 = d * d;
    float d3 = d2 * d;

    if (d < 1.0f) {
        return (1.0f / 6.0f) * ((12.0f - 9.0f * B - 6.0f * C) * d3 +
                                (-18.0f + 12.0f * B + 6.0f * C) * d2 +
                                (6.0f - 2.0f * B));
    } else {
        return (1.0f / 6.0f) * ((-B - 6.0f * C) * d3 +
                                (6.0f * B + 30.0f * C) * d2 +
                                (-12.0f * B - 48.0f * C) * d +
                                (8.0f * B + 24.0f * C));
    }
}

__device__ inline float evaluateFilter2DGpu(float dx, float dy, const GpuFilterSettings& f) {
    float wx = 0.0f, wy = 0.0f;
    if (f.type == 0) { // Box
        wx = evalBox1DGpu(dx, f.radiusX);
        wy = evalBox1DGpu(dy, f.radiusY);
    } else if (f.type == 1) { // Triangle
        wx = evalTriangle1DGpu(dx, f.radiusX);
        wy = evalTriangle1DGpu(dy, f.radiusY);
    } else if (f.type == 2) { // Gaussian
        wx = evalGaussian1DGpu(dx, f.radiusX, f.gaussianAlpha);
        wy = evalGaussian1DGpu(dy, f.radiusY, f.gaussianAlpha);
    } else { // Mitchell-Netravali
        wx = evalMitchellNetravali1DGpu(dx, f.radiusX, f.mitchellB, f.mitchellC);
        wy = evalMitchellNetravali1DGpu(dy, f.radiusY, f.mitchellB, f.mitchellC);
    }
    return wx * wy;
}

__device__ inline float radicalInverseBaseGpu(uint32_t index, uint32_t base) {
    float f = 1.0f / float(base);
    float result = 0.0f;
    uint32_t i = index;
    while (i > 0) {
        uint32_t digit = i % base;
        result += float(digit) * f;
        i /= base;
        f /= float(base);
    }
    return result;
}

__device__ inline float sobolBase2Gpu(uint32_t index, int dimension) {
    uint32_t gray = index ^ (index >> 1);
    uint32_t result = 0;
    if (dimension == 0) {
        for (int j = 1; j <= 32; ++j) {
            if ((gray >> (j - 1)) & 1u) result ^= (1u << (32 - j));
        }
    } else {
        uint32_t m = 1;
        for (int j = 1; j <= 32; ++j) {
            if (j > 1) m = (2u * m) ^ m;
            if ((gray >> (j - 1)) & 1u) result ^= (m << (32 - j));
        }
    }
    return float(result) / 4294967296.0f;
}

__device__ inline void getSubpixelOffsetGpu(int sampleIdx, int totalSamples,
                                            uint32_t frameIndex, uint32_t ix, uint32_t iy,
                                            const GpuFilterSettings& f, PCG32Device& rng,
                                            float& outDx, float& outDy) {
    float u1 = 0.0f, u2 = 0.0f;
    if (f.sampler == 0) { // Random
        u1 = rng.nextFloat();
        u2 = rng.nextFloat();
    } else if (f.sampler == 1) { // Stratified
        int gridSide = int(ceilf(sqrtf(float(totalSamples))));
        if (gridSide < 1) gridSide = 1;
        int sx = sampleIdx % gridSide;
        int sy = sampleIdx / gridSide;
        u1 = (float(sx) + rng.nextFloat()) / float(gridSide);
        u2 = (float(sy) + rng.nextFloat()) / float(gridSide);
    } else if (f.sampler == 2) { // Halton
        uint32_t idx = frameIndex * uint32_t(totalSamples) + uint32_t(sampleIdx) + 1u;
        float shiftX = float((ix * 1337 + iy * 5147) % 10000) / 10000.0f;
        float shiftY = float((ix * 7193 + iy * 3119) % 10000) / 10000.0f;
        u1 = radicalInverseBaseGpu(idx, 2) + shiftX;
        u2 = radicalInverseBaseGpu(idx, 3) + shiftY;
        u1 -= floorf(u1);
        u2 -= floorf(u2);
    } else { // Sobol
        uint32_t idx = frameIndex * uint32_t(totalSamples) + uint32_t(sampleIdx);
        float shiftX = float((ix * 1337 + iy * 5147) % 10000) / 10000.0f;
        float shiftY = float((ix * 7193 + iy * 3119) % 10000) / 10000.0f;
        u1 = sobolBase2Gpu(idx, 0) + shiftX;
        u2 = sobolBase2Gpu(idx, 1) + shiftY;
        u1 -= floorf(u1);
        u2 -= floorf(u2);
    }
    outDx = (u1 - 0.5f) * (2.0f * f.radiusX);
    outDy = (u2 - 0.5f) * (2.0f * f.radiusY);
}

extern "C" __global__ void __raygen__pt() {
    const uint32_t ix = optixGetLaunchIndex().x;
    const uint32_t iy = optixGetLaunchIndex().y;
    if (ix >= g_params.width || iy >= g_params.height) return;

    PRD prd;
    prd.rng.seed((uint64_t)iy * 4973 + (uint64_t)ix * 9277 + g_params.frameIndex * 1337,
                 (uint64_t)iy * 3511 + (uint64_t)ix * 4999);

    int spp = max(1, g_params.filter.raysPerPixel);
    float3 weightedRadiance = make_float3(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    uint32_t u0, u1;
    packPointer(&prd, u0, u1);

    for (int s = 0; s < spp; ++s) {
        float dx = 0.0f, dy = 0.0f;
        getSubpixelOffsetGpu(s, spp, g_params.frameIndex, ix, iy, g_params.filter, prd.rng, dx, dy);

        float u = (float(ix) + 0.5f + dx) / float(g_params.width);
        float v = (float(iy) + 0.5f + dy) / float(g_params.height);

        float weight = evaluateFilter2DGpu(dx, dy, g_params.filter);
        if (weight <= 0.0f) continue;

        float3 rayOrig = g_params.camera.origin;
        float3 rayDir  = normalize(g_params.camera.lowerLeftCorner +
                                   u * g_params.camera.horizontal +
                                   v * g_params.camera.vertical - rayOrig);

        float3 radiance   = make_float3(0.0f, 0.0f, 0.0f);
        float3 throughput = make_float3(1.0f, 1.0f, 1.0f);

        for (int depth = 0; depth <= g_params.maxDepth; ++depth) {
            prd.hit = false;

            optixTrace(
                g_params.traversable,
                rayOrig, rayDir,
                1e-4f, 1e30f, 0.0f,
                OptixVisibilityMask(255),
                OPTIX_RAY_FLAG_DISABLE_ANYHIT,
                0, 1, 0,
                u0, u1
            );

            if (!prd.hit) {
                radiance = radiance + throughput * missColor(rayDir);
                break;
            }

            radiance   = radiance + throughput * prd.emission;
            throughput = throughput * prd.attenuation;

            if (depth >= 3) {
                float p = fmaxf(throughput.x, fmaxf(throughput.y, throughput.z));
                p = fminf(p, 0.95f);
                if (prd.rng.nextFloat() >= p) break;
                throughput = throughput / p;
            }

            rayOrig = prd.nextOrigin;
            rayDir  = prd.nextDirection;
        }

        weightedRadiance = weightedRadiance + weight * radiance;
        totalWeight += weight;
    }

    float3 frameRadiance = (totalWeight > 1e-6f) ? (weightedRadiance / totalWeight) : make_float3(0.0f, 0.0f, 0.0f);

    const uint32_t pixelIdx = iy * g_params.width + ix;
    uint32_t accumIdx = pixelIdx * 4;
    float n = float(g_params.sampleCount);

    if (g_params.sampleCount == 0) {
        g_params.accumBuffer[accumIdx + 0] = frameRadiance.x;
        g_params.accumBuffer[accumIdx + 1] = frameRadiance.y;
        g_params.accumBuffer[accumIdx + 2] = frameRadiance.z;
        g_params.accumBuffer[accumIdx + 3] = 1.0f;
    } else {
        g_params.accumBuffer[accumIdx + 0] = (g_params.accumBuffer[accumIdx + 0] * n + frameRadiance.x) / (n + 1.0f);
        g_params.accumBuffer[accumIdx + 1] = (g_params.accumBuffer[accumIdx + 1] * n + frameRadiance.y) / (n + 1.0f);
        g_params.accumBuffer[accumIdx + 2] = (g_params.accumBuffer[accumIdx + 2] * n + frameRadiance.z) / (n + 1.0f);
    }

    g_params.frameBuffer[pixelIdx] = make_uchar4(
        gammaEncode(g_params.accumBuffer[accumIdx + 0]),
        gammaEncode(g_params.accumBuffer[accumIdx + 1]),
        gammaEncode(g_params.accumBuffer[accumIdx + 2]),
        255
    );
}


extern "C" __global__ void __miss__sky() {
    PRD* prd = getPRD();
    prd->hit = false;
}


extern "C" __global__ void __closesthit__pt() {
   PRD* prd = getPRD();
    prd->hit = true;

    const uint32_t primID = optixGetPrimitiveIndex();
    const GpuTriangle tri = g_params.triangles[primID];

    const float2 barys = optixGetTriangleBarycentrics();
    const float u = barys.x;
    const float v = barys.y;
    const float w = 1.0f - u - v;

    float3 N_raw = normalize(w * tri.n0 + u * tri.n1 + v * tri.n2);
    float3 rayDir = optixGetWorldRayDirection();
    float3 rayOrig = optixGetWorldRayOrigin();
    float  tHit = optixGetRayTmax();

    float3 P = rayOrig + tHit * rayDir;

    uint32_t matId = (tri.materialId < g_params.numMaterials) ? tri.materialId : 0;
    const GpuMaterial mat = g_params.materials[matId];

    prd->emission = mat.emission;

    float r1 = prd->rng.nextFloat();
    float r2 = prd->rng.nextFloat();

    float3 nextDir;

    // --- MATERIAL 0: DIFFUSE (Lambertiano) ---
    if (mat.type == 0) {
        float3 N = (dot(N_raw, -rayDir) < 0.0f) ? -N_raw : N_raw;
        float3 local = cosineSampleHemisphere(r1, r2);
        nextDir = toWorld(local, N);
        prd->attenuation = mat.albedo;

    // --- MATERIAL 1: SPECULAR (Espejo Ideal) ---
    } else if (mat.type == 1) {
        float3 N = (dot(N_raw, -rayDir) < 0.0f) ? -N_raw : N_raw;
        nextDir = normalize(reflect(rayDir, N));
        prd->attenuation = mat.albedo;

    // --- MATERIAL 2: DIELECTRIC (Cristal/Refracción/Fresnel) ---
    } else {
        float refRatio;
        float3 outN;
        float cosine;
        bool front = dot(rayDir, N_raw) < 0.0f;

        if (front) {
            outN     = N_raw;
            refRatio = 1.0f / mat.ior;
            cosine   = -dot(rayDir, N_raw);
        } else {
            outN     = -N_raw;
            refRatio = mat.ior;
            cosine   = dot(rayDir, N_raw);
        }

        float sinT2 = refRatio * refRatio * (1.0f - cosine * cosine);
        float F     = schlick(cosine, mat.ior);
        float3 refracted;
        bool tir    = (sinT2 > 1.0f);

        if (tir || r1 < F) {
            nextDir = normalize(reflect(rayDir, outN));
        } else {
            if (!refract(rayDir, outN, refRatio, refracted))
                nextDir = normalize(reflect(rayDir, outN));
            else
                nextDir = refracted;
        }
        prd->attenuation = mat.albedo;
    }

    prd->nextOrigin    = P + 1e-4f * nextDir;
    prd->nextDirection = nextDir;
}