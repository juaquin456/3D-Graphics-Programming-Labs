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

extern "C" __global__ void __raygen__pt() {
    const uint32_t ix = optixGetLaunchIndex().x;
    const uint32_t iy = optixGetLaunchIndex().y;
    if (ix >= g_params.width || iy >= g_params.height) return;

    PRD prd;
    prd.rng.seed((uint64_t)iy * 4973 + (uint64_t)ix * 9277 + g_params.frameIndex * 1337,
                 (uint64_t)iy * 3511 + (uint64_t)ix * 4999);

    // Muestreo con Jittering sub-píxel
    float u = (float(ix) + prd.rng.nextFloat()) / float(g_params.width);
    float v = (float(iy) + prd.rng.nextFloat()) / float(g_params.height);

    float3 rayOrig = g_params.camera.origin;
    float3 rayDir  = normalize(g_params.camera.lowerLeftCorner +
                               u * g_params.camera.horizontal +
                               v * g_params.camera.vertical - rayOrig);

    float3 radiance   = make_float3(0.0f, 0.0f, 0.0f);
    float3 throughput = make_float3(1.0f, 1.0f, 1.0f);

    uint32_t u0, u1;
    packPointer(&prd, u0, u1);

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

    const uint32_t pixelIdx = iy * g_params.width + ix;
    uint32_t accumIdx = pixelIdx * 4;
    float n = float(g_params.sampleCount);

    if (g_params.sampleCount == 0) {
        g_params.accumBuffer[accumIdx + 0] = radiance.x;
        g_params.accumBuffer[accumIdx + 1] = radiance.y;
        g_params.accumBuffer[accumIdx + 2] = radiance.z;
        g_params.accumBuffer[accumIdx + 3] = 1.0f;
    } else {
        g_params.accumBuffer[accumIdx + 0] = (g_params.accumBuffer[accumIdx + 0] * n + radiance.x) / (n + 1.0f);
        g_params.accumBuffer[accumIdx + 1] = (g_params.accumBuffer[accumIdx + 1] * n + radiance.y) / (n + 1.0f);
        g_params.accumBuffer[accumIdx + 2] = (g_params.accumBuffer[accumIdx + 2] * n + radiance.z) / (n + 1.0f);
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