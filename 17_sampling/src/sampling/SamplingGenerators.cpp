#include "SamplingGenerators.h"

#include <random>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <limits>
#include <iostream>

namespace Sampling {


std::vector<glm::vec3> PseudoRandomGenerator::generate(size_t count, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    std::vector<glm::vec3> points;
    points.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        points.emplace_back(dist(rng), dist(rng), dist(rng));
    }
    return points;
}


float HaltonGenerator::radicalInverse(uint32_t index, uint32_t base) {
    float f = 1.0f / static_cast<float>(base);
    float result = 0.0f;
    uint32_t i = index;
    while (i > 0) {
        uint32_t digit = i % base;
        result += static_cast<float>(digit) * f;
        i /= base;
        f /= static_cast<float>(base);
    }
    return result;
}

float HaltonGenerator::scrambledRadicalInverse(uint32_t index, uint32_t base, const std::vector<uint32_t>& perm) {
    float f = 1.0f / static_cast<float>(base);
    float result = 0.0f;
    uint32_t i = index;
    while (i > 0) {
        uint32_t digit = i % base;
        uint32_t scrambledDigit = (digit < perm.size()) ? perm[digit] : digit;
        result += static_cast<float>(scrambledDigit) * f;
        i /= base;
        f /= static_cast<float>(base);
    }
    return result;
}

std::vector<uint32_t> HaltonGenerator::getFaurePermutation(uint32_t base) {

    std::vector<uint32_t> perm(base);
    std::iota(perm.begin(), perm.end(), 0);

    if (base == 2) {
        return perm;
    }

    for (uint32_t i = 1; i < base; ++i) {
        uint32_t target = (i * (base - 1)) % (base - 1) + 1;
        perm[i] = target;
    }
    return perm;
}

std::vector<glm::vec3> HaltonGenerator::generate(size_t count,
                                                 const std::array<uint32_t, 3>& bases,
                                                 bool scramble,
                                                 uint32_t offset) {
    std::vector<glm::vec3> points;
    points.reserve(count);

    std::vector<uint32_t> permX, permY, permZ;
    if (scramble) {
        permX = getFaurePermutation(bases[0]);
        permY = getFaurePermutation(bases[1]);
        permZ = getFaurePermutation(bases[2]);
    }

    for (size_t i = 0; i < count; ++i) {
        uint32_t idx = static_cast<uint32_t>(i + offset);
        float x, y, z;
        if (scramble) {
            x = scrambledRadicalInverse(idx, bases[0], permX);
            y = scrambledRadicalInverse(idx, bases[1], permY);
            z = scrambledRadicalInverse(idx, bases[2], permZ);
        } else {
            x = radicalInverse(idx, bases[0]);
            y = radicalInverse(idx, bases[1]);
            z = radicalInverse(idx, bases[2]);
        }
        points.emplace_back(x, y, z);
    }
    return points;
}

SobolGenerator::SobolGenerator() {
    initDirectionNumbers();
}

void SobolGenerator::initDirectionNumbers() {

    for (int j = 1; j <= 32; ++j) {
        m_V[0][j - 1] = 1u << (32 - j);
    }

    uint32_t m2[33] = {0};
    m2[1] = 1;
    m_V[1][0] = m2[1] << 31;
    for (int j = 2; j <= 32; ++j) {
        m2[j] = (2u * m2[j - 1]) ^ m2[j - 1];
        m_V[1][j - 1] = m2[j] << (32 - j);
    }


    uint32_t m3[33] = {0};
    m3[1] = 1;
    m3[2] = 3;
    m_V[2][0] = m3[1] << 31;
    m_V[2][1] = m3[2] << 30;
    for (int j = 3; j <= 32; ++j) {
        m3[j] = (2u * m3[j - 1]) ^ (4u * m3[j - 2]) ^ m3[j - 2];
        m_V[2][j - 1] = m3[j] << (32 - j);
    }
}

glm::vec3 SobolGenerator::sample(uint32_t index, bool digitalShift,
                                 const std::array<uint32_t, 3>& shift) const {
    uint32_t gray = index ^ (index >> 1);
    glm::vec3 pt(0.0f);

    for (int d = 0; d < 3; ++d) {
        uint32_t val = 0;
        for (int j = 0; j < 32; ++j) {
            if ((gray >> j) & 1u) {
                val ^= m_V[d][j];
            }
        }
        if (digitalShift) {
            val ^= shift[d];
        }
        pt[d] = static_cast<float>(val) / 4294967296.0f;
    }

    return pt;
}

std::vector<glm::vec3> SobolGenerator::generate(size_t count, bool digitalShift, uint32_t seed) const {
    std::array<uint32_t, 3> shift = {0, 0, 0};
    if (digitalShift) {
        std::mt19937 rng(seed == 0 ? 1337u : seed);
        shift[0] = static_cast<uint32_t>(rng());
        shift[1] = static_cast<uint32_t>(rng());
        shift[2] = static_cast<uint32_t>(rng());
    }

    std::vector<glm::vec3> points;
    points.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        points.push_back(sample(static_cast<uint32_t>(i), digitalShift, shift));
    }
    return points;
}

std::vector<float> MetricsCalculator::computeNearestNeighborDistances(const std::vector<glm::vec3>& points) {
    size_t n = points.size();
    std::vector<float> nnDists(n, std::numeric_limits<float>::infinity());

    if (n < 2) return nnDists;

    #pragma omp parallel for
    for (size_t i = 0; i < n; ++i) {
        float minDistSq = std::numeric_limits<float>::infinity();
        const glm::vec3& p1 = points[i];
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            const glm::vec3& p2 = points[j];
            float dx = p1.x - p2.x;
            float dy = p1.y - p2.y;
            float dz = p1.z - p2.z;
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq < minDistSq) {
                minDistSq = distSq;
            }
        }
        nnDists[i] = std::sqrt(minDistSq);
    }
    return nnDists;
}

SequenceMetrics MetricsCalculator::compute(const std::vector<glm::vec3>& points, int gridK) {
    SequenceMetrics metrics;
    size_t n = points.size();
    if (n < 2) return metrics;

    auto nnDists = computeNearestNeighborDistances(points);

    float sumDist = 0.0f;
    metrics.minDist = std::numeric_limits<float>::infinity();
    metrics.maxDist = 0.0f;

    for (float d : nnDists) {
        if (d < metrics.minDist) metrics.minDist = d;
        if (d > metrics.maxDist) metrics.maxDist = d;
        sumDist += d;
    }
    metrics.meanNearestDist = sumDist / static_cast<float>(n);

    float sumSqDiff = 0.0f;
    for (float d : nnDists) {
        float diff = d - metrics.meanNearestDist;
        sumSqDiff += diff * diff;
    }
    metrics.stdNearestDist = std::sqrt(sumSqDiff / static_cast<float>(n));

    int totalCells = gridK * gridK * gridK;
    metrics.totalVoxels = totalCells;
    std::vector<int> counts(totalCells, 0);

    for (const auto& p : points) {
        int gx = std::clamp(static_cast<int>(p.x * gridK), 0, gridK - 1);
        int gy = std::clamp(static_cast<int>(p.y * gridK), 0, gridK - 1);
        int gz = std::clamp(static_cast<int>(p.z * gridK), 0, gridK - 1);
        int cellIdx = gx + gridK * (gy + gridK * gz);
        counts[cellIdx]++;
    }

    float expectedPerCell = static_cast<float>(n) / static_cast<float>(totalCells);
    float chiSq = 0.0f;
    int emptyCount = 0;

    for (int c : counts) {
        if (c == 0) emptyCount++;
        float diff = static_cast<float>(c) - expectedPerCell;
        chiSq += (diff * diff) / expectedPerCell;
    }

    metrics.chiSquare = chiSq;
    metrics.emptyVoxels = emptyCount;

    return metrics;
}

}
