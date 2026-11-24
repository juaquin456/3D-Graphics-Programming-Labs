#include "NoiseGenerator.h"
#include <glm/glm.hpp>
#include <cmath>
#include <vector>
#include <random>
#include <iostream>
#include <algorithm>

namespace vrm {

namespace {

// ============================================================================
// UTILIDADES MATEMÁTICAS PERIÓDICAS
// ============================================================================

// Polinomio de suavizado de grado 5 (Perlin 2002: sin segunda derivada)
inline float smooth5(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Función hash periódica para enteros (período = power of 2)
// Retorna un valor pseudo-aleatorio en [0, 1] dado (ix, iy, iz, seed)
inline float hashf(int ix, int iy, int iz, int seed) {
    uint32_t h = static_cast<uint32_t>(ix + iy * 2003 + iz * 4007 + seed * 8017);
    h ^= h >> 16;
    h *= 0x45d9f3bU;
    h ^= h >> 16;
    h *= 0x45d9f3bU;
    h ^= h >> 16;
    return static_cast<float>(h & 0xffffff) / static_cast<float>(0xffffff);
}

// Vector gradiente de Perlin (12 vectores de arista del cubo unitario)
glm::vec3 perlinGrad(int ix, int iy, int iz, int period, int seed) {
    int px = ((ix % period) + period) % period;
    int py = ((iy % period) + period) % period;
    int pz = ((iz % period) + period) % period;

    // Hash determinista que produce uno de los 12 gradientes clásicos de Perlin
    uint32_t h = static_cast<uint32_t>(px + py * 571 + pz * 1327 + seed * 2053);
    h ^= h >> 13;
    h *= 0x5bd1e995U;
    h ^= h >> 15;
    int idx = static_cast<int>(h % 12);

    // Los 12 vectores de arista del cubo {±1, ±1, 0}, {±1, 0, ±1}, {0, ±1, ±1}
    const glm::vec3 grads[12] = {
        { 1,  1,  0}, {-1,  1,  0}, { 1, -1,  0}, {-1, -1,  0},
        { 1,  0,  1}, {-1,  0,  1}, { 1,  0, -1}, {-1,  0, -1},
        { 0,  1,  1}, { 0, -1,  1}, { 0,  1, -1}, { 0, -1, -1}
    };
    return grads[idx];
}

// ============================================================================
// RUIDO DE PERLIN PERIÓDICO (TILEABLE)
// ============================================================================
// Entrada: posición en espacio de celda (p.x ∈ [0, period)), period debe ser
// potencia de 2 para seamless tileability.
float tileablePerlin(float x, float y, float z, int period, int seed) {
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    int z0 = static_cast<int>(std::floor(z));

    float fx = x - std::floor(x);
    float fy = y - std::floor(y);
    float fz = z - std::floor(z);

    float u = smooth5(fx);
    float v = smooth5(fy);
    float w = smooth5(fz);

    // 8 vértices de la celda con wrapping periódico
    float dot000 = glm::dot(perlinGrad(x0,   y0,   z0,   period, seed), glm::vec3(fx,       fy,       fz      ));
    float dot100 = glm::dot(perlinGrad(x0+1, y0,   z0,   period, seed), glm::vec3(fx - 1.f, fy,       fz      ));
    float dot010 = glm::dot(perlinGrad(x0,   y0+1, z0,   period, seed), glm::vec3(fx,       fy - 1.f, fz      ));
    float dot110 = glm::dot(perlinGrad(x0+1, y0+1, z0,   period, seed), glm::vec3(fx - 1.f, fy - 1.f, fz      ));
    float dot001 = glm::dot(perlinGrad(x0,   y0,   z0+1, period, seed), glm::vec3(fx,       fy,       fz - 1.f));
    float dot101 = glm::dot(perlinGrad(x0+1, y0,   z0+1, period, seed), glm::vec3(fx - 1.f, fy,       fz - 1.f));
    float dot011 = glm::dot(perlinGrad(x0,   y0+1, z0+1, period, seed), glm::vec3(fx,       fy - 1.f, fz - 1.f));
    float dot111 = glm::dot(perlinGrad(x0+1, y0+1, z0+1, period, seed), glm::vec3(fx - 1.f, fy - 1.f, fz - 1.f));

    float x0y0 = glm::mix(dot000, dot100, u);
    float x1y0 = glm::mix(dot010, dot110, u);
    float x0y1 = glm::mix(dot001, dot101, u);
    float x1y1 = glm::mix(dot011, dot111, u);

    float y0r = glm::mix(x0y0, x1y0, v);
    float y1r = glm::mix(x0y1, x1y1, v);

    // Mapear [-1, 1] → [0, 1]
    return glm::clamp(glm::mix(y0r, y1r, w) * 0.5f + 0.5f, 0.0f, 1.0f);
}

// ============================================================================
// RUIDO DE WORLEY PERIÓDICO (CELLULAR / F1 DISTANCE)
// ============================================================================
// Retorna la DISTANCIA mínima al feature-point más cercano, normalizada a [0,1].
// El Worley INVERTIDO (1 - dist) crea los "bultos redondeados" de las nubes.
float tileableWorley(float x, float y, float z, int cellCount, int seed) {
    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));
    int zi = static_cast<int>(std::floor(z));

    // Distancia mínima al feature-point (F1)
    float minDist = 1e9f;

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                // Wrapping periódico de la celda vecina
                int cx = ((xi + dx) % cellCount + cellCount) % cellCount;
                int cy = ((yi + dy) % cellCount + cellCount) % cellCount;
                int cz = ((zi + dz) % cellCount + cellCount) % cellCount;

                // Feature-point aleatorio dentro de la celda (3 hashes independientes)
                float fpx = static_cast<float>(cx) + hashf(cx, cy, cz, seed * 3 + 0);
                float fpy = static_cast<float>(cy) + hashf(cx, cy, cz, seed * 3 + 1);
                float fpz = static_cast<float>(cz) + hashf(cx, cy, cz, seed * 3 + 2);

                // Desplazamiento real de la celda vecina en espacio de coordenadas
                float realX = static_cast<float>(xi + dx);
                float realY = static_cast<float>(yi + dy);
                float realZ = static_cast<float>(zi + dz);

                // Distancia al feature-point con corrección de wrapping periódico
                float diffX = fpx - (x + (realX - static_cast<float>(xi + dx)));
                float diffY = fpy - (y + (realY - static_cast<float>(yi + dy)));
                float diffZ = fpz - (z + (realZ - static_cast<float>(zi + dz)));

                // Recalculamos correctamente: posición del fp relativo al punto de consulta
                diffX = fpx - (x - static_cast<float>(xi) + static_cast<float>(xi + dx));
                diffY = fpy - (y - static_cast<float>(yi) + static_cast<float>(yi + dy));
                diffZ = fpz - (z - static_cast<float>(zi) + static_cast<float>(zi + dz));

                float dist = std::sqrt(diffX*diffX + diffY*diffY + diffZ*diffZ);
                if (dist < minDist) minDist = dist;
            }
        }
    }

    // Normalizar al rango [0, 1] asumiendo max dist ≈ 1 celda
    return glm::clamp(minDist, 0.0f, 1.0f);
}

// ============================================================================
// PERLIN-WORLEY FBM (4 OCTAVAS)
// ============================================================================
// Composición clave para nubes cúmulo:
//   perlin da la forma suave de gran escala
//   worley invertido add los bultos redondeados tipo "coliflor"
// canal R: perlin-worley combinado (macro-forma)
// canal G: worley invertido fino (erosión de borde)
struct CloudTexel {
    float perlinWorley;  // macro forma: Perlin modulado por Worley
    float worleyFine;    // detalle fino: Worley invertido a alta frecuencia
};

CloudTexel sampleCloudTexel(float fx, float fy, float fz) {
    // === CANAL R: Perlin-Worley (macro forma) ===
    // Frecuencia base 4 celdas, 4 octavas FBM
    float pw = 0.0f;
    float amp = 0.625f;
    float freq = 4.0f;
    for (int oct = 0; oct < 4; ++oct) {
        float p  = tileablePerlin(fx * freq, fy * freq, fz * freq,
                                   static_cast<int>(freq), 42 + oct * 17);
        float w  = tileableWorley(fx * freq, fy * freq, fz * freq,
                                   static_cast<int>(freq), 137 + oct * 29);
        // Remap: Perlin modulado por (1-Worley).
        // Cuando worley ≈ 0 (lejos del feature-point) → usa Perlin sin atenuar.
        // Cuando worley ≈ 1 (en el feature-point, centro del bulto) → Perlin se atenúa.
        // Esto produce abultamientos redondeados incrustados en la forma suave de Perlin.
        float remapped = p * (1.0f - w);
        // Re-remap a [0,1] con saturación
        remapped = glm::clamp(remapped / 0.5f, 0.0f, 1.0f);
        pw  += amp * remapped;
        amp  *= 0.5f;
        freq *= 2.03f;  // Lacunarity ligeramente irracional → evita artefactos de repetición
    }
    pw = glm::clamp(pw, 0.0f, 1.0f);

    // === CANAL G: Worley Invertido fino (detalle de erosión de borde) ===
    // 3 octavas a frecuencia alta (8, 16, 32 celdas)
    float wFine = 0.0f;
    float wAmp  = 0.625f;
    float wFreq = 8.0f;
    for (int oct = 0; oct < 3; ++oct) {
        float w = tileableWorley(fx * wFreq, fy * wFreq, fz * wFreq,
                                  static_cast<int>(wFreq), 293 + oct * 41);
        wFine += wAmp * (1.0f - w);  // invertir: 1 → bultos, 0 → espacio entre celdas
        wAmp  *= 0.5f;
        wFreq *= 2.03f;
    }
    wFine = glm::clamp(wFine, 0.0f, 1.0f);

    return { pw, wFine };
}

} // anonymous namespace

// ============================================================================
// GENERADOR PRINCIPAL: Textura 3D 128^3 tileable (GL_RGBA8)
// Canal R: Perlin-Worley FBM (macro forma de nube)
// Canal G: Worley Invertido FBM (erosión fina de borde)
// Canal B/A: reservados (Worley detalle extra)
// Tiempo de cómputo estimado: ~2-4 s a 128^3 en CPU
// ============================================================================
GLuint NoiseGenerator::generateTileable3DNoise(int resolution) {
    std::cout << "[NoiseGenerator] Precomputing " << resolution
              << "^3 Perlin-Worley tileable 3D noise texture..." << std::endl;
    std::cout << "[NoiseGenerator]   Canal R: Perlin-Worley FBM 4-octavas (macro forma)" << std::endl;
    std::cout << "[NoiseGenerator]   Canal G: Worley Invertido FBM 3-octavas (erosion de borde)" << std::endl;

    const int N = resolution;
    std::vector<uint8_t> data(N * N * N * 4, 0);

    for (int z = 0; z < N; ++z) {
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                // Coordenadas normalizadas en [0, 1) — periódicas en los bordes
                float fx = static_cast<float>(x) / static_cast<float>(N);
                float fy = static_cast<float>(y) / static_cast<float>(N);
                float fz = static_cast<float>(z) / static_cast<float>(N);

                CloudTexel t = sampleCloudTexel(fx, fy, fz);

                // Canal B: Worley simple de alta frecuencia (detalle extra para nieve/hielo)
                float w32 = tileableWorley(fx * 16.0f, fy * 16.0f, fz * 16.0f, 16, 701);
                float wB  = glm::clamp(1.0f - w32, 0.0f, 1.0f);

                // Canal A: Perlin de muy baja frecuencia (gradiente de densidad base)
                float pLow = tileablePerlin(fx * 2.0f, fy * 2.0f, fz * 2.0f, 2, 999);

                int idx = (z * N * N + y * N + x) * 4;
                data[idx + 0] = static_cast<uint8_t>(t.perlinWorley * 255.0f + 0.5f);
                data[idx + 1] = static_cast<uint8_t>(t.worleyFine   * 255.0f + 0.5f);
                data[idx + 2] = static_cast<uint8_t>(wB             * 255.0f + 0.5f);
                data[idx + 3] = static_cast<uint8_t>(pLow           * 255.0f + 0.5f);
            }
        }
    }

    // Subir a GPU como GL_TEXTURE_3D GL_REPEAT GL_LINEAR
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_3D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage3D(GL_TEXTURE_3D,
                 0,              // mip level
                 GL_RGBA8,       // internal format
                 N, N, N,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 data.data());

    // GL_REPEAT = tileable sin costura en los 3 ejes
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
    // GL_LINEAR = interpolación trilineal por hardware (sin aliasing de vóxeles)
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_3D);

    glBindTexture(GL_TEXTURE_3D, 0);

    std::cout << "[NoiseGenerator] Listo: " << (data.size() / 1024) << " KB en CPU -> GL_TEXTURE_3D." << std::endl;
    return tex;
}
} // namespace vrm