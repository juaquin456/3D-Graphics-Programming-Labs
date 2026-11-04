#include "PointCloudBuilder.h"
#include <cmath>
#include <algorithm>
#include <glm/gtc/constants.hpp>

glm::vec3 PointCloudBuilder::turboColormap(float t) {
    // Polynomial approximation of Turbo / Rainbow colormap
    t = std::clamp(t, 0.0f, 1.0f);
    const glm::vec4 kRedVec4(-0.564472f, 2.766722f, -2.570887f, 1.378949f);
    const glm::vec4 kGreenVec4(0.339474f, 2.748364f, -4.514758f, 2.433947f);
    const glm::vec4 kBlueVec4(0.999650f, -0.686520f, -2.000624f, 1.688173f);

    float r = 0.13572138f + t * (4.61539260f + t * (kRedVec4.x + t * (kRedVec4.y + t * (kRedVec4.z + t * kRedVec4.w))));
    float g = 0.09140261f + t * (2.19418839f + t * (kGreenVec4.x + t * (kGreenVec4.y + t * (kGreenVec4.z + t * kGreenVec4.w))));
    float b = 0.10667330f + t * (12.64194608f + t * (kBlueVec4.x + t * (kBlueVec4.y + t * (kBlueVec4.z + t * kBlueVec4.w))));

    return glm::clamp(glm::vec3(r, g, b), 0.0f, 1.0f);
}

glm::vec3 PointCloudBuilder::heatmapColormap(float t) {
    // Blue -> Green -> Red heatmap
    t = std::clamp(t, 0.0f, 1.0f);
    float r = std::clamp(1.5f - std::abs(t - 1.0f) * 3.0f, 0.0f, 1.0f);
    float g = std::clamp(1.5f - std::abs(t - 0.5f) * 3.0f, 0.0f, 1.0f);
    float b = std::clamp(1.5f - std::abs(t - 0.0f) * 3.0f, 0.0f, 1.0f);
    return glm::vec3(r, g, b);
}

MeshData PointCloudBuilder::createBoundingBoxMesh(const glm::vec3& minCorner, const glm::vec3& maxCorner, float thickness) {
    MeshData mesh;
    float h = thickness * 0.5f;

    // Helper to add a thin box beam between two points
    auto addBeam = [&](const glm::vec3& p0, const glm::vec3& p1) {
        glm::vec3 d = p1 - p0;
        float len = glm::length(d);
        if (len < 1e-6f) return;
        glm::vec3 dir = d / len;

        // Choose arbitrary perpendicular vectors
        glm::vec3 up = (std::abs(dir.y) < 0.9f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 right = glm::normalize(glm::cross(dir, up)) * h;
        up = glm::normalize(glm::cross(right, dir)) * h;

        int baseIdx = static_cast<int>(mesh.vertices.size());

        // 8 vertices for the cuboid beam
        glm::vec3 v0 = p0 - right - up;
        glm::vec3 v1 = p0 + right - up;
        glm::vec3 v2 = p0 + right + up;
        glm::vec3 v3 = p0 - right + up;

        glm::vec3 v4 = p1 - right - up;
        glm::vec3 v5 = p1 + right - up;
        glm::vec3 v6 = p1 + right + up;
        glm::vec3 v7 = p1 - right + up;

        mesh.vertices.push_back(v0);
        mesh.vertices.push_back(v1);
        mesh.vertices.push_back(v2);
        mesh.vertices.push_back(v3);
        mesh.vertices.push_back(v4);
        mesh.vertices.push_back(v5);
        mesh.vertices.push_back(v6);
        mesh.vertices.push_back(v7);

        for (int i = 0; i < 8; ++i) {
            mesh.normals.push_back(glm::vec3(0, 1, 0));
            mesh.uvs.push_back(glm::vec2(0.5f, 0.5f));
            mesh.tangents.push_back(glm::vec3(0.85f, 0.85f, 0.85f));
        }

        // 6 faces (12 triangles)
        int faces[6][4] = {
            {0, 1, 2, 3}, // Start cap
            {4, 7, 6, 5}, // End cap
            {0, 4, 5, 1}, // Side 1
            {1, 5, 6, 2}, // Side 2
            {2, 6, 7, 3}, // Side 3
            {3, 7, 4, 0}  // Side 4
        };

        for (int f = 0; f < 6; ++f) {
            mesh.indices.push_back(baseIdx + faces[f][0]);
            mesh.indices.push_back(baseIdx + faces[f][1]);
            mesh.indices.push_back(baseIdx + faces[f][2]);

            mesh.indices.push_back(baseIdx + faces[f][0]);
            mesh.indices.push_back(baseIdx + faces[f][2]);
            mesh.indices.push_back(baseIdx + faces[f][3]);
        }
    };

    glm::vec3 c[8] = {
        {minCorner.x, minCorner.y, minCorner.z}, // 0
        {maxCorner.x, minCorner.y, minCorner.z}, // 1
        {maxCorner.x, maxCorner.y, minCorner.z}, // 2
        {minCorner.x, maxCorner.y, minCorner.z}, // 3
        {minCorner.x, minCorner.y, maxCorner.z}, // 4
        {maxCorner.x, minCorner.y, maxCorner.z}, // 5
        {maxCorner.x, maxCorner.y, maxCorner.z}, // 6
        {minCorner.x, maxCorner.y, maxCorner.z}  // 7
    };

    // 12 edges of the cube
    addBeam(c[0], c[1]); addBeam(c[1], c[2]); addBeam(c[2], c[3]); addBeam(c[3], c[0]); // Bottom loop
    addBeam(c[4], c[5]); addBeam(c[5], c[6]); addBeam(c[6], c[7]); addBeam(c[7], c[4]); // Top loop
    addBeam(c[0], c[4]); addBeam(c[1], c[5]); addBeam(c[2], c[6]); addBeam(c[3], c[7]); // Pillars

    mesh.recompute_normals();
    return mesh;
}

MeshData PointCloudBuilder::createBatchedMiniballs(const std::vector<glm::vec3>& points,
                                                   float radius,
                                                   int slices,
                                                   int stacks,
                                                   ColorMode colorMode,
                                                   const std::vector<float>& nnDistances,
                                                   const glm::vec3& baseColor) {
    MeshData combinedMesh;
    size_t numPoints = points.size();
    if (numPoints == 0) return combinedMesh;

    // Precompute a canonical sphere centered at (0, 0, 0)
    MeshData unitSphere = MeshData::NewSphere(radius, slices, stacks);
    size_t sphereVertCount = unitSphere.vertices.size();
    size_t sphereIndexCount = unitSphere.indices.size();

    combinedMesh.vertices.reserve(numPoints * sphereVertCount);
    combinedMesh.normals.reserve(numPoints * sphereVertCount);
    combinedMesh.uvs.reserve(numPoints * sphereVertCount);
    combinedMesh.tangents.reserve(numPoints * sphereVertCount);
    combinedMesh.indices.reserve(numPoints * sphereIndexCount);

    float minNN = 1e9f, maxNN = -1e9f;
    if (colorMode == ColorMode::DensityHeatmap && !nnDistances.empty()) {
        for (float d : nnDistances) {
            if (d < minNN) minNN = d;
            if (d > maxNN) maxNN = d;
        }
        if (maxNN <= minNN) maxNN = minNN + 1e-4f;
    }

    for (size_t i = 0; i < numPoints; ++i) {
        const glm::vec3& center = points[i];
        int baseIndex = static_cast<int>(combinedMesh.vertices.size());

        // Determine color for this ball
        glm::vec3 ballColor = baseColor;
        if (colorMode == ColorMode::CoordinatesRGB) {
            ballColor = glm::clamp(center, 0.0f, 1.0f);
        } else if (colorMode == ColorMode::SequenceIndex) {
            float t = static_cast<float>(i) / static_cast<float>(std::max<size_t>(1, numPoints - 1));
            ballColor = turboColormap(t);
        } else if (colorMode == ColorMode::DensityHeatmap) {
            if (!nnDistances.empty() && i < nnDistances.size()) {
                float normD = (nnDistances[i] - minNN) / (maxNN - minNN);
                ballColor = heatmapColormap(normD);
            }
        }

        // Append sphere vertices offset by center
        for (size_t v = 0; v < sphereVertCount; ++v) {
            combinedMesh.vertices.push_back(center + unitSphere.vertices[v]);
            combinedMesh.normals.push_back(v < unitSphere.normals.size() ? unitSphere.normals[v] : glm::vec3(0, 1, 0));
            glm::vec2 uvCoord = (!unitSphere.uvs.empty() && v < unitSphere.uvs.size()) ? unitSphere.uvs[v] : glm::vec2(0.5f, 0.5f);
            combinedMesh.uvs.push_back(uvCoord);
            combinedMesh.tangents.push_back(ballColor); // Use tangent attribute for per-ball RGB color
        }

        // Append shifted indices
        for (size_t idx = 0; idx < sphereIndexCount; ++idx) {
            combinedMesh.indices.push_back(baseIndex + unitSphere.indices[idx]);
        }
    }

    return combinedMesh;
}

void PointCloudBuilder::updateScene(Scene& scene,
                                    const SamplingConfig& config,
                                    const Sampling::SobolGenerator& sobolGen,
                                    Sampling::SequenceMetrics& outMetricsSingle,
                                    Sampling::SequenceMetrics& outMetricsRandom,
                                    Sampling::SequenceMetrics& outMetricsHalton,
                                    Sampling::SequenceMetrics& outMetricsSobol) {
    scene.objects.clear();

    std::array<uint32_t, 3> haltonBases = {
        static_cast<uint32_t>(config.haltonBaseX),
        static_cast<uint32_t>(config.haltonBaseY),
        static_cast<uint32_t>(config.haltonBaseZ)
    };

    auto adjustPoints = [&](std::vector<glm::vec3>& pts) {
        for (auto& p : pts) {
            if (config.flattenZ) {
                p.z = 0.5f; // Project onto Z=0.5 plane
            }
            if (config.centerAtOrigin) {
                p -= glm::vec3(0.5f);
            }
        }
    };

    glm::vec3 minBox = config.centerAtOrigin ? glm::vec3(-0.5f) : glm::vec3(0.0f);
    glm::vec3 maxBox = config.centerAtOrigin ? glm::vec3(0.5f)  : glm::vec3(1.0f);

    auto boxMat = std::make_shared<PhongMaterial>(
        glm::vec3(0.3f), glm::vec3(0.85f, 0.85f, 0.9f), glm::vec3(0.5f), 16.0f
    );

    if (config.generatorType == GeneratorType::SideBySide) {
        // Compute all 3 sets
        auto randomPts = Sampling::PseudoRandomGenerator::generate(config.sampleCount, config.randomSeed);
        auto haltonPts = Sampling::HaltonGenerator::generate(config.sampleCount, haltonBases, config.haltonScramble);
        auto sobolPts  = sobolGen.generate(config.sampleCount, config.sobolDigitalShift, config.sobolSeed);

        outMetricsRandom = Sampling::MetricsCalculator::compute(randomPts);
        outMetricsHalton = Sampling::MetricsCalculator::compute(haltonPts);
        outMetricsSobol  = Sampling::MetricsCalculator::compute(sobolPts);

        auto nnRandom = Sampling::MetricsCalculator::computeNearestNeighborDistances(randomPts);
        auto nnHalton = Sampling::MetricsCalculator::computeNearestNeighborDistances(haltonPts);
        auto nnSobol  = Sampling::MetricsCalculator::computeNearestNeighborDistances(sobolPts);

        adjustPoints(randomPts);
        adjustPoints(haltonPts);
        adjustPoints(sobolPts);

        // Box 1: Pseudo-Random (Offset X = -1.4)
        float offsetX_rand = -1.35f;
        MeshData randMesh = createBatchedMiniballs(randomPts, config.ballRadius, config.sphereSlices, config.sphereStacks,
                                                    config.colorMode, nnRandom, glm::vec3(0.95f, 0.45f, 0.35f)); // Coral
        auto randAsset = std::make_shared<MeshAsset>(randMesh);
        RenderObject randObj(randAsset, std::make_shared<PhongMaterial>(glm::vec3(0.2f), glm::vec3(0.95f, 0.45f, 0.35f), glm::vec3(0.8f), 32.0f));
        randObj.position = glm::vec3(offsetX_rand, 0.0f, 0.0f);
        scene.objects.push_back(randObj);

        // Box 2: Halton (Center X = 0.0)
        MeshData haltonMesh = createBatchedMiniballs(haltonPts, config.ballRadius, config.sphereSlices, config.sphereStacks,
                                                     config.colorMode, nnHalton, glm::vec3(0.25f, 0.85f, 0.45f)); // Emerald
        auto haltonAsset = std::make_shared<MeshAsset>(haltonMesh);
        RenderObject haltonObj(haltonAsset, std::make_shared<PhongMaterial>(glm::vec3(0.2f), glm::vec3(0.25f, 0.85f, 0.45f), glm::vec3(0.8f), 32.0f));
        scene.objects.push_back(haltonObj);

        // Box 3: Sobol (Offset X = +1.4)
        float offsetX_sobol = 1.35f;
        MeshData sobolMesh = createBatchedMiniballs(sobolPts, config.ballRadius, config.sphereSlices, config.sphereStacks,
                                                    config.colorMode, nnSobol, glm::vec3(0.25f, 0.65f, 0.95f)); // Cyan
        auto sobolAsset = std::make_shared<MeshAsset>(sobolMesh);
        RenderObject sobolObj(sobolAsset, std::make_shared<PhongMaterial>(glm::vec3(0.2f), glm::vec3(0.25f, 0.65f, 0.95f), glm::vec3(0.8f), 32.0f));
        sobolObj.position = glm::vec3(offsetX_sobol, 0.0f, 0.0f);
        scene.objects.push_back(sobolObj);

        if (config.showBoundingBox) {
            MeshData boxMesh = createBoundingBoxMesh(minBox, maxBox, config.boxThickness);
            auto boxAsset = std::make_shared<MeshAsset>(boxMesh);

            RenderObject b1(boxAsset, boxMat); b1.position = glm::vec3(offsetX_rand, 0, 0); scene.objects.push_back(b1);
            RenderObject b2(boxAsset, boxMat); b2.position = glm::vec3(0.0f, 0, 0);         scene.objects.push_back(b2);
            RenderObject b3(boxAsset, boxMat); b3.position = glm::vec3(offsetX_sobol, 0, 0);scene.objects.push_back(b3);
        }
    } else {
        // Single Sequence Mode
        std::vector<glm::vec3> pts;
        glm::vec3 themeColor(0.2f, 0.7f, 0.9f);

        if (config.generatorType == GeneratorType::PseudoRandom) {
            pts = Sampling::PseudoRandomGenerator::generate(config.sampleCount, config.randomSeed);
            themeColor = glm::vec3(0.95f, 0.45f, 0.35f);
        } else if (config.generatorType == GeneratorType::Halton) {
            pts = Sampling::HaltonGenerator::generate(config.sampleCount, haltonBases, config.haltonScramble);
            themeColor = glm::vec3(0.25f, 0.85f, 0.45f);
        } else if (config.generatorType == GeneratorType::Sobol) {
            pts = sobolGen.generate(config.sampleCount, config.sobolDigitalShift, config.sobolSeed);
            themeColor = glm::vec3(0.25f, 0.65f, 0.95f);
        }

        outMetricsSingle = Sampling::MetricsCalculator::compute(pts);
        auto nnDists = Sampling::MetricsCalculator::computeNearestNeighborDistances(pts);

        adjustPoints(pts);

        MeshData ballsMesh = createBatchedMiniballs(pts, config.ballRadius, config.sphereSlices, config.sphereStacks,
                                                    config.colorMode, nnDists, themeColor);
        auto ballsAsset = std::make_shared<MeshAsset>(ballsMesh);
        RenderObject ballsObj(ballsAsset, std::make_shared<PhongMaterial>(glm::vec3(0.2f), themeColor, glm::vec3(0.8f), 32.0f));
        scene.objects.push_back(ballsObj);

        if (config.showBoundingBox) {
            MeshData boxMesh = createBoundingBoxMesh(minBox, maxBox, config.boxThickness);
            auto boxAsset = std::make_shared<MeshAsset>(boxMesh);
            RenderObject boxObj(boxAsset, boxMat);
            scene.objects.push_back(boxObj);
        }
    }
}
