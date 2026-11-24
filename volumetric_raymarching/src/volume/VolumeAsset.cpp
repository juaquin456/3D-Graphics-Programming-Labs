#include "VolumeAsset.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <cstdio>

namespace vrm {

namespace {

bool parseVec3Field(const std::string& json, const std::string& key, glm::vec3& outVec) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return false;
    size_t start = json.find('[', pos);
    size_t end = json.find(']', start);
    if (start == std::string::npos || end == std::string::npos) return false;

    std::string slice = json.substr(start + 1, end - start - 1);
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (std::sscanf(slice.c_str(), "%f , %f , %f", &x, &y, &z) == 3) {
        outVec = glm::vec3(x, y, z);
        return true;
    }
    return false;
}

bool parseIVec3Field(const std::string& json, const std::string& key, glm::ivec3& outVec) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return false;
    size_t start = json.find('[', pos);
    size_t end = json.find(']', start);
    if (start == std::string::npos || end == std::string::npos) return false;

    std::string slice = json.substr(start + 1, end - start - 1);
    int x = 0, y = 0, z = 0;
    if (std::sscanf(slice.c_str(), "%d , %d , %d", &x, &y, &z) == 3) {
        outVec = glm::ivec3(x, y, z);
        return true;
    }
    return false;
}

std::vector<std::string> parseStringArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return result;
    size_t start = json.find('[', pos);
    size_t end = json.find(']', start);
    if (start == std::string::npos || end == std::string::npos) return result;

    std::string slice = json.substr(start + 1, end - start - 1);
    size_t cursor = 0;
    while (cursor < slice.size()) {
        size_t q1 = slice.find('"', cursor);
        if (q1 == std::string::npos) break;
        size_t q2 = slice.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        result.push_back(slice.substr(q1 + 1, q2 - q1 - 1));
        cursor = q2 + 1;
    }
    return result;
}

} // namespace

VolumeAsset::VolumeAsset()
    : m_resolution(128, 128, 128),
      m_aspectRatio(0.6923f, 1.0f, 0.6923f),
      m_boxMin(-0.3462f, 0.0f, -0.3462f),
      m_boxMax(0.3462f, 1.0f, 0.3462f),
      m_texCurrent(0),
      m_texNext(0),
      m_uploadedCurrentIdx(-1),
      m_uploadedNextIdx(-1),
      m_currentFrame(0),
      m_nextFrame(0),
      m_frameBlend(0.0f),
      m_accumulatedTime(0.0f),
      m_frameDuration(1.0f / 30.0f) // 30 fps suaves para secuencias de 200 frames
{}

VolumeAsset::~VolumeAsset() {
    cleanup();
}

void VolumeAsset::cleanup() {
    if (m_texCurrent != 0) {
        glDeleteTextures(1, &m_texCurrent);
        m_texCurrent = 0;
    }
    if (m_texNext != 0) {
        glDeleteTextures(1, &m_texNext);
        m_texNext = 0;
    }
    m_frameBuffers.clear();
    m_uploadedCurrentIdx = -1;
    m_uploadedNextIdx = -1;
    m_currentFrame = 0;
    m_nextFrame = 0;
    m_frameBlend = 0.0f;
}

GLuint VolumeAsset::allocateEmptyTexture3D(int width, int height, int depth) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_3D, tex);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Reserva el almacenamiento en VRAM una sola vez (4 MB)
    glTexImage3D(
        GL_TEXTURE_3D,
        0,
        GL_RG8,
        width, height, depth,
        0,
        GL_RG,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    float borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_3D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glBindTexture(GL_TEXTURE_3D, 0);
    return tex;
}

void VolumeAsset::uploadFrameToTexture(GLuint tex, int frameIndex) {
    if (tex == 0 || frameIndex < 0 || frameIndex >= static_cast<int>(m_frameBuffers.size())) {
        return;
    }

    glBindTexture(GL_TEXTURE_3D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Actualiza en sitio sin realocar VRAM (~0.15 ms por actualización)
    glTexSubImage3D(
        GL_TEXTURE_3D,
        0,
        0, 0, 0,
        m_resolution.x, m_resolution.y, m_resolution.z,
        GL_RG,
        GL_UNSIGNED_BYTE,
        m_frameBuffers[frameIndex].data()
    );

    glBindTexture(GL_TEXTURE_3D, 0);
}

void VolumeAsset::syncGpuTextures() {
    if (m_frameBuffers.empty()) return;

    if (m_uploadedCurrentIdx != m_currentFrame) {
        uploadFrameToTexture(m_texCurrent, m_currentFrame);
        m_uploadedCurrentIdx = m_currentFrame;
    }
    if (m_uploadedNextIdx != m_nextFrame) {
        uploadFrameToTexture(m_texNext, m_nextFrame);
        m_uploadedNextIdx = m_nextFrame;
    }
}

bool VolumeAsset::loadFromDirectory(const std::string& dirPath) {
    cleanup();

    std::string metaPath = dirPath + "/metadata.json";
    std::ifstream metaFile(metaPath);
    if (!metaFile.is_open()) {
        std::cerr << "[VolumeAsset ERROR] Could not open metadata file: " << metaPath << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << metaFile.rdbuf();
    std::string jsonStr = buffer.str();

    parseIVec3Field(jsonStr, "grid_resolution", m_resolution);
    parseVec3Field(jsonStr, "aspect_ratio", m_aspectRatio);
    parseVec3Field(jsonStr, "box_min", m_boxMin);
    parseVec3Field(jsonStr, "box_max", m_boxMax);

    std::vector<std::string> frameNames = parseStringArray(jsonStr, "frames");
    if (frameNames.empty()) {
        int numFrames = 24;
        size_t numPos = jsonStr.find("\"num_frames\":");
        if (numPos != std::string::npos) {
            numFrames = std::stoi(jsonStr.substr(numPos + 13, 8));
        }
        for (int i = 0; i < numFrames; ++i) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "frame_%04d.raw", i);
            frameNames.emplace_back(buf);
        }
    }

    std::cout << "[VolumeAsset] Loading " << frameNames.size() << " volume frames ("
              << m_resolution.x << "x" << m_resolution.y << "x" << m_resolution.z
              << ") into system RAM from " << dirPath << "..." << std::endl;

    size_t expectedBytes = static_cast<size_t>(m_resolution.x) *
                           static_cast<size_t>(m_resolution.y) *
                           static_cast<size_t>(m_resolution.z) * 2;

    m_frameBuffers.reserve(frameNames.size());

    for (const std::string& fname : frameNames) {
        std::string rawPath = dirPath + "/" + fname;
        std::ifstream rawFile(rawPath, std::ios::binary);
        if (!rawFile.is_open()) {
            std::cerr << "[VolumeAsset ERROR] Could not open raw frame: " << rawPath << std::endl;
            continue;
        }

        std::vector<uint8_t> frameData(expectedBytes);
        rawFile.read(reinterpret_cast<char*>(frameData.data()), expectedBytes);
        if (rawFile.gcount() != static_cast<std::streamsize>(expectedBytes)) {
            std::cerr << "[VolumeAsset WARNING] Incomplete read for " << rawPath << std::endl;
            continue;
        }

        m_frameBuffers.push_back(std::move(frameData));
    }

    if (m_frameBuffers.empty()) {
        return false;
    }

    // Asignar solo 2 texturas 3D en VRAM (8 MB totales sin importar cuántos frames haya)
    m_texCurrent = allocateEmptyTexture3D(m_resolution.x, m_resolution.y, m_resolution.z);
    m_texNext    = allocateEmptyTexture3D(m_resolution.x, m_resolution.y, m_resolution.z);

    m_currentFrame = 0;
    m_nextFrame = (m_frameBuffers.size() > 1) ? 1 : 0;
    m_frameBlend = 0.0f;
    m_accumulatedTime = 0.0f;

    syncGpuTextures();

    size_t ramMB = (m_frameBuffers.size() * expectedBytes) / (1024 * 1024);
    size_t vramMB = (2 * expectedBytes) / (1024 * 1024);
    std::cout << "[VolumeAsset] Ready: " << m_frameBuffers.size() << " frames in RAM ("
              << ramMB << " MB) | GPU streaming footprint: " << vramMB << " MB VRAM." << std::endl;

    return true;
}

void VolumeAsset::update(float deltaTime, float speed, bool isPlaying) {
    if (m_frameBuffers.empty()) return;

    if (isPlaying) {
        m_accumulatedTime += deltaTime * speed;
        float totalDuration = static_cast<float>(m_frameBuffers.size()) * m_frameDuration;
        float wrappedTime = std::fmod(m_accumulatedTime, totalDuration);
        if (wrappedTime < 0.0f) wrappedTime += totalDuration;

        float floatFrame = wrappedTime / m_frameDuration;
        int total = static_cast<int>(m_frameBuffers.size());
        m_currentFrame = static_cast<int>(floatFrame) % total;
        m_nextFrame = (m_currentFrame + 1) % total;
        m_frameBlend = floatFrame - std::floor(floatFrame);

        syncGpuTextures();
    }
}

void VolumeAsset::setManualFrame(float frameFraction) {
    if (m_frameBuffers.empty()) return;

    float clamped = glm::clamp(frameFraction, 0.0f, 0.9999f);
    int total = static_cast<int>(m_frameBuffers.size());
    float floatFrame = clamped * static_cast<float>(total);

    m_currentFrame = static_cast<int>(floatFrame) % total;
    m_nextFrame = (m_currentFrame + 1) % total;
    m_frameBlend = floatFrame - std::floor(floatFrame);

    syncGpuTextures();
}

} // namespace vrm