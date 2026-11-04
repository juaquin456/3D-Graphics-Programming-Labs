#ifndef SAMPLING_GENERATORS_H
#define SAMPLING_GENERATORS_H

#include <vector>
#include <array>
#include <cstdint>
#include <string>
#include <glm/vec3.hpp>

namespace Sampling {

struct SequenceMetrics {
    float minDist{0.0f};          // Smallest distance between any two points (measures clustering/clumping)
    float maxDist{0.0f};          // Maximum distance
    float meanNearestDist{0.0f};  // Average distance to nearest neighbor
    float stdNearestDist{0.0f};   // Standard deviation of nearest neighbor distance (spatial uniformity variance)
    float chiSquare{0.0f};        // Chi-Square test over a KxKxK voxel grid (expected vs observed counts)
    int emptyVoxels{0};           // Number of empty voxel cells in KxKxK grid (voids)
    int totalVoxels{64};          // Total voxels (default 4x4x4 = 64)
};

class PseudoRandomGenerator {
public:
    static std::vector<glm::vec3> generate(size_t count, uint32_t seed = 42);
};

class HaltonGenerator {
public:
    static float radicalInverse(uint32_t index, uint32_t base);

    static float scrambledRadicalInverse(uint32_t index, uint32_t base, const std::vector<uint32_t>& perm);

    static std::vector<uint32_t> getFaurePermutation(uint32_t base);

    static std::vector<glm::vec3> generate(size_t count,
                                           const std::array<uint32_t, 3>& bases = {2, 3, 5},
                                           bool scramble = false,
                                           uint32_t offset = 1);
};

class SobolGenerator {
public:
    SobolGenerator();

    glm::vec3 sample(uint32_t index, bool digitalShift = false,
                     const std::array<uint32_t, 3>& shift = {0, 0, 0}) const;

    std::vector<glm::vec3> generate(size_t count, bool digitalShift = false, uint32_t seed = 0) const;

private:
    uint32_t m_V[3][32];
    void initDirectionNumbers();
};

class MetricsCalculator {
public:
    static SequenceMetrics compute(const std::vector<glm::vec3>& points, int gridK = 4);
    static std::vector<float> computeNearestNeighborDistances(const std::vector<glm::vec3>& points);
};

}

#endif // SAMPLING_GENERATORS_H
