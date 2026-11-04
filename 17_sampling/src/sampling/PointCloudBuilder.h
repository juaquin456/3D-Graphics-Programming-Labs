#ifndef POINT_CLOUD_BUILDER_H
#define POINT_CLOUD_BUILDER_H

#include <vector>
#include <memory>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include "common/Scene.h"
#include "common/MeshData.h"
#include "common/Material.h"
#include "SamplingGenerators.h"

enum class GeneratorType {
    PseudoRandom = 0,
    Halton = 1,
    Sobol = 2,
    SideBySide = 3
};

enum class ColorMode {
    Uniform = 0,
    CoordinatesRGB = 1,
    SequenceIndex = 2,
    DensityHeatmap = 3
};

struct SamplingConfig {
    GeneratorType generatorType{GeneratorType::Sobol};
    int sampleCount{512};
    float ballRadius{0.018f};
    int sphereSlices{8};
    int sphereStacks{6};

    // Halton parameters
    int haltonBaseX{2};
    int haltonBaseY{3};
    int haltonBaseZ{5};
    bool haltonScramble{false};
    int haltonPreset{0}; // 0: (2,3,5), 1: (2,3,7), 2: (7,11,13), 3: (11,13,17 - High correlation)

    // Sobol parameters
    bool sobolDigitalShift{false};
    uint32_t sobolSeed{1337};

    // Random parameters
    uint32_t randomSeed{42};

    // Visualization parameters
    ColorMode colorMode{ColorMode::CoordinatesRGB};
    bool centerAtOrigin{true};   // Center [-0.5, 0.5]^3 vs [0, 1]^3
    bool flattenZ{false};        // 2D projection to Z=0 to inspect patterns
    bool showBoundingBox{true};
    float boxThickness{0.004f};
};

class PointCloudBuilder {
public:
    static MeshData createBoundingBoxMesh(const glm::vec3& minCorner, const glm::vec3& maxCorner, float thickness = 0.003f);
    static MeshData createBatchedMiniballs(const std::vector<glm::vec3>& points,
                                           float radius,
                                           int slices,
                                           int stacks,
                                           ColorMode colorMode,
                                           const std::vector<float>& nnDistances = {},
                                           const glm::vec3& baseColor = glm::vec3(0.2f, 0.7f, 0.9f));

    static void updateScene(Scene& scene,
                            const SamplingConfig& config,
                            const Sampling::SobolGenerator& sobolGen,
                            Sampling::SequenceMetrics& outMetricsSingle,
                            Sampling::SequenceMetrics& outMetricsRandom,
                            Sampling::SequenceMetrics& outMetricsHalton,
                            Sampling::SequenceMetrics& outMetricsSobol);

    // Color conversion helpers
    static glm::vec3 turboColormap(float t);
    static glm::vec3 heatmapColormap(float t);
};

#endif // POINT_CLOUD_BUILDER_H
