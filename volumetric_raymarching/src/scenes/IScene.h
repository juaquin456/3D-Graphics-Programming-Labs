#pragma once

#include <glm/glm.hpp>
#include "../core/Shader.h"
#include "../renderer/SceneGeometry.h"
#include "../volume/VolumeAsset.h"

namespace vrm {

enum class SceneType {
    CampfireVDB = 0,
    ProceduralCloud = 1,
    GodRaysGeometry = 2
};

struct SceneSettings {
    SceneType type = SceneType::CampfireVDB;

    // Optical parameters
    float sigmaA = 6.5f;             // Absorption coefficient (Beer-Lambert law)
    float sigmaS = 4.5f;             // Scattering coefficient
    float henyeyG = 0.55f;           // Anisotropy parameter g in [-0.8, 0.9]
    float emissionIntensity = 4.2f;  // Real-time Blackbody radiation intensity
    float densityMultiplier = 1.25f;

    // Ray marching sampling quality (Cinematic defaults)
    int maxSteps = 128;              // 128 steps default for smooth, crisp density
    float stepSize = 0.012f;         // Fine world-space step size
    bool enableJitter = true;        // Interleaved Gradient Noise dithering
    bool enableSelfShadow = true;    // Secondary shadow ray integration
    int shadowSteps = 6;             // 6 shadow steps for self-shadowing accuracy
    bool showGround = true;          // Render procedural ground plane
    bool halfRes = false;            // Native 1.0x Full-Resolution by default
    int debugMode = 0;               // 0: Normal, 1: Step Heatmap, 2: Density Only
    float exposure = 1.0f;           // ACES tone-mapping exposure scale

    // Primary Lighting
    glm::vec3 lightPos = glm::vec3(0.9f, 1.6f, 0.9f);
    glm::vec3 lightColor = glm::vec3(1.0f, 0.94f, 0.88f);
    float lightIntensity = 2.8f;
    bool orbitLight = false;

    // Spotlight parameters (Scene 3)
    glm::vec3 spotlightTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    float spotlightCutoff = 35.0f;   // degrees

    // Animation & Playback (Scene 1)
    bool isPlaying = true;
    float playbackSpeed = 1.0f;
    float manualFrame = 0.0f;

    // Bounding Box (AABB)
    glm::vec3 boxMin = glm::vec3(-0.35f, 0.0f, -0.35f);
    glm::vec3 boxMax = glm::vec3( 0.35f, 1.0f,  0.35f);
};

} // namespace vrm
