#pragma once

#include <embree4/rtcore.h>
#include <vector>
#include <string>
#include <cstdint>
#include <chrono>

#include "SceneTypes.h"


class EmbreePathTracer {
public:
    EmbreePathTracer() = default;
    ~EmbreePathTracer() { cleanup(); }

    static bool isAvailable();

    bool init();
    void uploadScene(const std::vector<PathTriangle>& triangles,
                     const std::vector<PathMaterial>& materials);

    double render(const RayGenParams& camera, const PTSettings& settings,
                  unsigned char* outFB, int width, int height);

    void resetAccumulation() { m_sampleCount = 0; m_frameIndex = 0; }
    uint32_t sampleCount() const { return m_sampleCount; }

    void cleanup();

private:
    RTCDevice m_device = nullptr;
    RTCScene  m_scene  = nullptr;

    std::vector<PathTriangle> m_triangles;
    std::vector<PathMaterial> m_materials;

    std::vector<float> m_accumBuf;
    uint32_t m_sampleCount = 0;
    uint32_t m_frameIndex  = 0;
    int      m_width       = 0;
    int      m_height      = 0;
};
