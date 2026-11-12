#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>

#ifdef __CUDACC__
#define FILTER_INLINE __host__ __device__ inline
#else
#define FILTER_INLINE inline
#endif

enum class FilterType : int {
    Box = 0,
    Triangle = 1,
    Gaussian = 2,
    MitchellNetravali = 3
};

enum class SubpixelSampler : int {
    Random = 0,
    Stratified = 1,
    Halton = 2,
    Sobol = 3
};

struct FilterSettings {
    FilterType      type{FilterType::Gaussian};
    SubpixelSampler sampler{SubpixelSampler::Random};
    int             raysPerPixel{4};       // Samples per pixel per frame (SPP)
    float           radiusX{1.5f};         // Filter extent / radius in pixels
    float           radiusY{1.5f};
    float           gaussianAlpha{2.0f};   // Falloff rate for Gaussian filter
    float           mitchellB{1.0f / 3.0f};// B parameter for Mitchell-Netravali
    float           mitchellC{1.0f / 3.0f};// C parameter for Mitchell-Netravali
};

namespace PixelFilter {

FILTER_INLINE float evalBox1D(float x, float radius) {
    return (std::abs(x) <= radius) ? 1.0f : 0.0f;
}

FILTER_INLINE float evalTriangle1D(float x, float radius) {
    float d = std::abs(x);
    if (d > radius || radius <= 1e-6f) return 0.0f;
    return 1.0f - d / radius;
}

// 3. Windowed Gaussian Filter
FILTER_INLINE float evalGaussian1D(float x, float radius, float alpha) {
    float d = std::abs(x);
    if (d > radius) return 0.0f;
    float e = std::exp(-alpha * d * d);
    float eEdge = std::exp(-alpha * radius * radius);
    return std::max(0.0f, e - eEdge);
}

// 4. Mitchell-Netravali Cubic Spline Filter
FILTER_INLINE float evalMitchellNetravali1D(float x, float radius, float B, float C) {
    if (radius <= 1e-6f) return 0.0f;
    float d = std::abs(x) * (2.0f / radius);
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

// 2D Separable Filter Evaluation: W(dx, dy) = f(dx) * f(dy)
FILTER_INLINE float evaluate2D(float dx, float dy, const FilterSettings& settings) {
    float wx = 0.0f;
    float wy = 0.0f;

    switch (settings.type) {
        case FilterType::Box:
            wx = evalBox1D(dx, settings.radiusX);
            wy = evalBox1D(dy, settings.radiusY);
            break;
        case FilterType::Triangle:
            wx = evalTriangle1D(dx, settings.radiusX);
            wy = evalTriangle1D(dy, settings.radiusY);
            break;
        case FilterType::Gaussian:
            wx = evalGaussian1D(dx, settings.radiusX, settings.gaussianAlpha);
            wy = evalGaussian1D(dy, settings.radiusY, settings.gaussianAlpha);
            break;
        case FilterType::MitchellNetravali:
            wx = evalMitchellNetravali1D(dx, settings.radiusX, settings.mitchellB, settings.mitchellC);
            wy = evalMitchellNetravali1D(dy, settings.radiusY, settings.mitchellB, settings.mitchellC);
            break;
    }
    return wx * wy;
}

// -------------------------------------------------------------------------
// Subpixel 2D Sample Generators
// -------------------------------------------------------------------------
FILTER_INLINE float radicalInverseBase(uint32_t index, uint32_t base) {
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

FILTER_INLINE float sobolBase2(uint32_t index, int dimension) {
    uint32_t gray = index ^ (index >> 1);
    uint32_t result = 0;
    if (dimension == 0) {
        for (int j = 1; j <= 32; ++j) {
            if ((gray >> (j - 1)) & 1u) {
                result ^= (1u << (32 - j));
            }
        }
    } else {
        uint32_t m = 1;
        for (int j = 1; j <= 32; ++j) {
            if (j > 1) {
                m = (2u * m) ^ m;
            }
            if ((gray >> (j - 1)) & 1u) {
                result ^= (m << (32 - j));
            }
        }
    }
    return static_cast<float>(result) / 4294967296.0f;
}


template <typename RngFunc>
FILTER_INLINE void getSubpixelOffset(int sampleIdx, int totalSamples,
                                     uint32_t frameIndex, uint32_t px, uint32_t py,
                                     const FilterSettings& settings,
                                     RngFunc&& rng,
                                     float& outDx, float& outDy) {
    float u1 = 0.0f;
    float u2 = 0.0f;

    switch (settings.sampler) {
        case SubpixelSampler::Random: {
            u1 = rng();
            u2 = rng();
            break;
        }
        case SubpixelSampler::Stratified: {
            int gridSide = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(totalSamples))));
            if (gridSide < 1) gridSide = 1;
            int sx = sampleIdx % gridSide;
            int sy = sampleIdx / gridSide;
            u1 = (static_cast<float>(sx) + rng()) / static_cast<float>(gridSide);
            u2 = (static_cast<float>(sy) + rng()) / static_cast<float>(gridSide);
            break;
        }
        case SubpixelSampler::Halton: {
            uint32_t idx = frameIndex * static_cast<uint32_t>(totalSamples) + static_cast<uint32_t>(sampleIdx) + 1u;
            // Cranley-Patterson shift per pixel to avoid cross-pixel aliasing
            float shiftX = static_cast<float>((px * 1337 + py * 5147) % 10000) / 10000.0f;
            float shiftY = static_cast<float>((px * 7193 + py * 3119) % 10000) / 10000.0f;
            u1 = radicalInverseBase(idx, 2) + shiftX;
            u2 = radicalInverseBase(idx, 3) + shiftY;
            u1 -= std::floor(u1);
            u2 -= std::floor(u2);
            break;
        }
        case SubpixelSampler::Sobol: {
            uint32_t idx = frameIndex * static_cast<uint32_t>(totalSamples) + static_cast<uint32_t>(sampleIdx);
            float shiftX = static_cast<float>((px * 1337 + py * 5147) % 10000) / 10000.0f;
            float shiftY = static_cast<float>((px * 7193 + py * 3119) % 10000) / 10000.0f;
            u1 = sobolBase2(idx, 0) + shiftX;
            u2 = sobolBase2(idx, 1) + shiftY;
            u1 -= std::floor(u1);
            u2 -= std::floor(u2);
            break;
        }
    }

    outDx = (u1 - 0.5f) * (2.0f * settings.radiusX);
    outDy = (u2 - 0.5f) * (2.0f * settings.radiusY);
}

}
