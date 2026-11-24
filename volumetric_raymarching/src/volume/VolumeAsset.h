#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace vrm {

    class VolumeAsset {
    public:
        VolumeAsset();
        ~VolumeAsset();

        bool loadFromDirectory(const std::string& dirPath);
        void cleanup();

        void update(float deltaTime, float speed, bool isPlaying);
        void setManualFrame(float frameFraction);

        GLuint getCurrentTexture() const { return m_texCurrent; }
        GLuint getNextTexture() const { return m_texNext; }
        float getFrameBlend() const { return m_frameBlend; }

        int getCurrentFrameIndex() const { return m_currentFrame; }
        int getNumFrames() const { return static_cast<int>(m_frameBuffers.size()); }

        const glm::ivec3& getResolution() const { return m_resolution; }
        const glm::vec3& getAspectRatio() const { return m_aspectRatio; }
        const glm::vec3& getBoxMin() const { return m_boxMin; }
        const glm::vec3& getBoxMax() const { return m_boxMax; }

    private:
        GLuint allocateEmptyTexture3D(int width, int height, int depth);
        void uploadFrameToTexture(GLuint tex, int frameIndex);
        void syncGpuTextures();

        glm::ivec3 m_resolution;
        glm::vec3 m_aspectRatio;
        glm::vec3 m_boxMin;
        glm::vec3 m_boxMax;

        // Frames almacenados en RAM del sistema (800 MB para 200 frames)
        std::vector<std::vector<uint8_t>> m_frameBuffers;

        // Solo 2 texturas 3D persistentes en VRAM (8 MB totales) para interpolación temporal
        GLuint m_texCurrent;
        GLuint m_texNext;
        int m_uploadedCurrentIdx;
        int m_uploadedNextIdx;

        int m_currentFrame;
        int m_nextFrame;
        float m_frameBlend;
        float m_accumulatedTime;
        float m_frameDuration;
    };

} // namespace vrm