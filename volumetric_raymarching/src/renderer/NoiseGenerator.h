#pragma once

#include <glad/glad.h>
#include <vector>
#include <cstdint>

namespace vrm {

class NoiseGenerator {
public:
    // Generates a 64x64x64 tileable 3D texture containing:
    // Channel R: Low-frequency Perlin noise
    // Channel G: Mid-frequency Worley noise
    // Channel B: High-frequency Worley noise
    // Channel A: Inverted combined Perlin-Worley noise
    static GLuint generateTileable3DNoise(int resolution = 64);
};

} // namespace vrm
