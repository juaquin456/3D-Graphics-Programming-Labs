#pragma once

#include <cstdint>
#include <glm/glm.hpp>

// ── Alias directo a GLM (Elimina todo el boilerplate de operadores) ──
using Vec3 = glm::vec3;

// ── Materiales BSDF ──
enum class MaterialType : uint32_t {
    Diffuse    = 0,
    Specular   = 1,
    Dielectric = 2
};

struct PathMaterial {
    MaterialType type      = MaterialType::Diffuse;
    Vec3         albedo    = {0.8f, 0.8f, 0.8f};
    Vec3         emission  = {0.0f, 0.0f, 0.0f};
    float        ior       = 1.5f;
    float        roughness = 0.0f;
};

// ── Geometría de Triángulo ──
struct PathTriangle {
    Vec3     v0, v1, v2;
    Vec3     n0, n1, n2;
    uint32_t materialId = 0;
};

struct RayGenParams {
    Vec3     origin;
    Vec3     lowerLeftCorner;
    Vec3     horizontal;
    Vec3     vertical;
    uint32_t frameIndex = 0;
};

struct PTSettings {
    int  maxDepth        = 6;
    int  samplesPerFrame = 1;
    bool accumulate      = true;
};;