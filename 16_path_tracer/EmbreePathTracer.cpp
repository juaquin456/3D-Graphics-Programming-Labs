#include "EmbreePathTracer.h"

#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>
#include <omp.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


struct PCG32Cpu {
    uint64_t state, inc;

    void seed(uint64_t s, uint64_t seq) {
        state = 0;
        inc   = (seq << 1) | 1u;
        next(); state += s; next();
    }
    uint32_t next() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        uint32_t xs  = ((old >> 18u) ^ old) >> 27u;
        uint32_t rot = old >> 59u;
        return (xs >> rot) | (xs << ((-rot) & 31));
    }
    float nextFloat() {
        return (next() >> 8) * (1.0f / 16777216.0f);
    }
};


static inline float schlickCpu(float cosine, float ior) {
    cosine = std::clamp(cosine, 0.0f, 1.0f);
    float r0 = (1.0f - ior) / (1.0f + ior);
    r0 *= r0;
    return r0 + (1.0f - r0) * std::pow(1.0f - cosine, 5.0f);
}

static inline glm::vec3 cosineSampleHemisphereCpu(float r1, float r2) {
    float phi = 6.28318530718f * r1;
    float st  = std::sqrt(r2);
    return glm::vec3(std::cos(phi) * st, std::sin(phi) * st, std::sqrt(1.0f - r2));
}

static inline glm::vec3 toWorldCpu(const glm::vec3& s, const glm::vec3& N) {
    glm::vec3 up = (std::abs(N.x) < 0.999f) ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 T  = glm::normalize(glm::cross(up, N));
    glm::vec3 B  = glm::cross(N, T);
    return glm::normalize(s.x * T + s.y * B + s.z * N);
}

static inline glm::vec3 missColorCpu(const glm::vec3& dir) {
    float t = 0.5f * (glm::normalize(dir).y + 1.0f);
    return (1.0f - t) * glm::vec3(1.0f, 1.0f, 1.0f) + t * glm::vec3(0.5f, 0.7f, 1.0f);
}

static inline unsigned char gammaEncodeCpu(float v) {
    float g = std::pow(std::clamp(v, 0.0f, 1.0f), 1.0f / 2.2f);
    return static_cast<unsigned char>(g * 255.99f);
}


bool EmbreePathTracer::isAvailable() {
    return true;
}

bool EmbreePathTracer::init() {
    m_device = rtcNewDevice(nullptr);
    if (!m_device) {
        std::cerr << "[Embree Error] No se pudo crear el dispositivo RTCDevice." << std::endl;
        return false;
    }
    return true;
}

void EmbreePathTracer::cleanup() {
    if (m_scene)  { rtcReleaseScene(m_scene); m_scene = nullptr; }
    if (m_device) { rtcReleaseDevice(m_device); m_device = nullptr; }
}


void EmbreePathTracer::uploadScene(const std::vector<PathTriangle>& triangles,
                                    const std::vector<PathMaterial>& materials) {
    m_triangles = triangles;
    m_materials = materials;
    resetAccumulation();

    if (m_scene) { rtcReleaseScene(m_scene); m_scene = nullptr; }
    if (m_triangles.empty()) return;

    m_scene = rtcNewScene(m_device);
    rtcSetSceneBuildQuality(m_scene, RTC_BUILD_QUALITY_HIGH);

    RTCGeometry geom = rtcNewGeometry(m_device, RTC_GEOMETRY_TYPE_TRIANGLE);

    float* vertices = static_cast<float*>(rtcSetNewGeometryBuffer(
        geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 3 * sizeof(float), m_triangles.size() * 3
    ));

    uint32_t* indices = static_cast<uint32_t*>(rtcSetNewGeometryBuffer(
        geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 3 * sizeof(uint32_t), m_triangles.size()
    ));

    for (size_t i = 0; i < m_triangles.size(); ++i) {
        const auto& tri = m_triangles[i];

        vertices[i * 9 + 0] = tri.v0.x; vertices[i * 9 + 1] = tri.v0.y; vertices[i * 9 + 2] = tri.v0.z;
        vertices[i * 9 + 3] = tri.v1.x; vertices[i * 9 + 4] = tri.v1.y; vertices[i * 9 + 5] = tri.v1.z;
        vertices[i * 9 + 6] = tri.v2.x; vertices[i * 9 + 7] = tri.v2.y; vertices[i * 9 + 8] = tri.v2.z;

        indices[i * 3 + 0] = static_cast<uint32_t>(i * 3 + 0);
        indices[i * 3 + 1] = static_cast<uint32_t>(i * 3 + 1);
        indices[i * 3 + 2] = static_cast<uint32_t>(i * 3 + 2);
    }

    rtcCommitGeometry(geom);
    rtcAttachGeometry(m_scene, geom);
    rtcReleaseGeometry(geom);

    rtcCommitScene(m_scene);
}

double EmbreePathTracer::render(const RayGenParams& camera, const PTSettings& settings,
                                 unsigned char* outFB, int width, int height) {
    if (!m_scene || !outFB || width <= 0 || height <= 0) return 0.0;

    if (m_width != width || m_height != height) {
        m_width  = width;
        m_height = height;
        m_accumBuf.assign(m_width * m_height * 4, 0.0f);
        resetAccumulation();
    }

    auto tStart = std::chrono::high_resolution_clock::now();

    #pragma omp parallel for schedule(dynamic, 1)
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            PCG32Cpu rng;
            rng.seed(static_cast<uint64_t>(py) * 4973 + static_cast<uint64_t>(px) * 9277 + m_frameIndex * 1337,
                     static_cast<uint64_t>(py) * 3511 + static_cast<uint64_t>(px) * 4999);

            float u = (static_cast<float>(px) + rng.nextFloat()) / static_cast<float>(width);
            float v = (static_cast<float>(py) + rng.nextFloat()) / static_cast<float>(height);

            glm::vec3 rayOrig = glm::vec3(camera.origin);
            glm::vec3 rayDir  = glm::normalize(glm::vec3(camera.lowerLeftCorner) +
                                              u * glm::vec3(camera.horizontal) +
                                              v * glm::vec3(camera.vertical) - rayOrig);

            glm::vec3 radiance(0.0f);
            glm::vec3 throughput(1.0f);

            for (int depth = 0; depth <= settings.maxDepth; ++depth) {
                alignas(16) RTCRayHit rayhit;
                rayhit.ray.org_x = rayOrig.x; rayhit.ray.org_y = rayOrig.y; rayhit.ray.org_z = rayOrig.z;
                rayhit.ray.dir_x = rayDir.x;  rayhit.ray.dir_y = rayDir.y;  rayhit.ray.dir_z = rayDir.z;
                rayhit.ray.tnear = 1e-4f;
                rayhit.ray.tfar  = 1e30f;
                rayhit.ray.mask  = -1;
                rayhit.ray.flags = 0;
                rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;

                rtcIntersect1(m_scene, &rayhit);

                if (rayhit.hit.geomID == RTC_INVALID_GEOMETRY_ID) {
                    radiance += throughput * missColorCpu(rayDir);
                    break;
                }

                uint32_t primID = rayhit.hit.primID;
                const PathTriangle& tri = m_triangles[primID];

                float hitU = rayhit.hit.u;
                float hitV = rayhit.hit.v;
                float hitW = 1.0f - hitU - hitV;

                glm::vec3 N_raw = glm::normalize(hitW * glm::vec3(tri.n0) + hitU * glm::vec3(tri.n1) + hitV * glm::vec3(tri.n2));
                glm::vec3 P     = rayOrig + rayhit.ray.tfar * rayDir;

                const PathMaterial& mat = m_materials[tri.materialId];
                radiance += throughput * glm::vec3(mat.emission);

                float r1 = rng.nextFloat();
                float r2 = rng.nextFloat();
                glm::vec3 nextDir;

                // --- DIFFUSE ---
                if (mat.type == MaterialType::Diffuse) {
                    glm::vec3 N = (glm::dot(N_raw, -rayDir) < 0.0f) ? -N_raw : N_raw;
                    glm::vec3 local = cosineSampleHemisphereCpu(r1, r2);
                    nextDir = toWorldCpu(local, N);
                    throughput *= glm::vec3(mat.albedo);

                // --- SPECULAR ---
                } else if (mat.type == MaterialType::Specular) {
                    glm::vec3 N = (glm::dot(N_raw, -rayDir) < 0.0f) ? -N_raw : N_raw;
                    nextDir = glm::normalize(glm::reflect(rayDir, N));
                    throughput *= glm::vec3(mat.albedo);

                // --- DIELECTRIC ---
                } else {
                    float refRatio;
                    glm::vec3 outN;
                    float cosine;
                    bool front = glm::dot(rayDir, N_raw) < 0.0f;

                    if (front) {
                        outN     = N_raw;
                        refRatio = 1.0f / mat.ior;
                        cosine   = -glm::dot(rayDir, N_raw);
                    } else {
                        outN     = -N_raw;
                        refRatio = mat.ior;
                        cosine   = glm::dot(rayDir, N_raw);
                    }

                    float sinT2 = refRatio * refRatio * (1.0f - cosine * cosine);
                    float F     = schlickCpu(cosine, mat.ior);
                    bool tir    = (sinT2 > 1.0f);

                    if (tir || r1 < F) {
                        nextDir = glm::normalize(glm::reflect(rayDir, outN));
                    } else {
                        nextDir = glm::normalize(glm::refract(rayDir, outN, refRatio));
                    }
                    throughput *= glm::vec3(mat.albedo);
                }

                if (depth >= 3) {
                    float p = std::max({throughput.r, throughput.g, throughput.b});
                    if (p <= 1e-4f || std::isnan(p)) break;
                    p = std::min(p, 0.95f);
                    if (rng.nextFloat() >= p) break;
                    throughput /= p;
                }

                rayOrig = P + 1e-4f * nextDir;
                rayDir  = nextDir;
            }

            if (std::isnan(radiance.r) || std::isnan(radiance.g) || std::isnan(radiance.b) ||
                std::isinf(radiance.r) || std::isinf(radiance.g) || std::isinf(radiance.b)) {
                radiance = glm::vec3(0.0f);
            }

            int pixelIdx = (py * width + px) * 4;
            float n = static_cast<float>(m_sampleCount);

            if (m_sampleCount == 0) {
                m_accumBuf[pixelIdx + 0] = radiance.r;
                m_accumBuf[pixelIdx + 1] = radiance.g;
                m_accumBuf[pixelIdx + 2] = radiance.b;
                m_accumBuf[pixelIdx + 3] = 1.0f;
            } else {
                m_accumBuf[pixelIdx + 0] = (m_accumBuf[pixelIdx + 0] * n + radiance.r) / (n + 1.0f);
                m_accumBuf[pixelIdx + 1] = (m_accumBuf[pixelIdx + 1] * n + radiance.g) / (n + 1.0f);
                m_accumBuf[pixelIdx + 2] = (m_accumBuf[pixelIdx + 2] * n + radiance.b) / (n + 1.0f);
            }

            outFB[pixelIdx + 0] = gammaEncodeCpu(m_accumBuf[pixelIdx + 0]);
            outFB[pixelIdx + 1] = gammaEncodeCpu(m_accumBuf[pixelIdx + 1]);
            outFB[pixelIdx + 2] = gammaEncodeCpu(m_accumBuf[pixelIdx + 2]);
            outFB[pixelIdx + 3] = 255;
        }
    }

    m_sampleCount++;
    m_frameIndex++;

    auto tEnd = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(tEnd - tStart).count();
}