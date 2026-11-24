#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <string>

#include "Camera.h"
#include "Shader.h"
#include "Framebuffer.h"
#include "../scenes/IScene.h"
#include "../renderer/SceneGeometry.h"
#include "../renderer/ShadowMap.h"
#include "../renderer/NoiseGenerator.h"
#include "../volume/VolumeAsset.h"
#include "../gui/DiagnosticUI.h"

namespace vrm {

class Application {
public:
    Application(int width = 1280, int height = 720, const std::string& title = "Real-Time Volumetric Ray Marching");
    ~Application();

    bool init();
    void run();

private:
    int m_windowWidth;
    int m_windowHeight;
    std::string m_windowTitle;
    GLFWwindow* m_window;

    Camera m_camera;
    SceneSettings m_settings;
    DiagnosticUI m_ui;

    // Assets & Resources
    VolumeAsset m_volumeAsset;
    GLuint m_noise3DTexture;
    SceneGeometry m_geometry;
    ShadowMap m_shadowMap;

    // Shaders
    Shader m_volumetricShader;
    Shader m_solidShader;
    Shader m_shadowShader;
    Shader m_compositeShader;

    // Framebuffers
    std::unique_ptr<Framebuffer> m_sceneFBO;
    std::unique_ptr<Framebuffer> m_volFullFBO;
    std::unique_ptr<Framebuffer> m_volHalfFBO;

    GLuint m_screenVao;

    // Interaction state
    bool m_isLeftDragging;
    bool m_isRightDragging;
    double m_lastMouseX;
    double m_lastMouseY;

    // Performance metrics
    float m_fps;
    float m_frameTimeMs;
    int m_frameCount;
    float m_fpsTimer;

    // Core methods
    void processInput(float deltaTime);
    void update(float deltaTime);
    void renderPassShadowMap();
    void renderPassSolidScene();
    void renderPassVolumetric();
    void renderPassComposite();
    void resizeFramebuffers(int width, int height);

    // Static GLFW callbacks
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
};

} // namespace vrm
