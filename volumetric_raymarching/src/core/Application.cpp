#include "Application.h"
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <filesystem>
#include <cmath>

namespace vrm {

namespace {

// Helper to resolve asset and shader paths relative to current or executable working directory
std::string resolvePath(const std::string& relativePath) {
    std::vector<std::string> searchPaths = {
        relativePath,
        "../" + relativePath,
        "../../" + relativePath,
        "volumetric_raymarching/" + relativePath,
        "../volumetric_raymarching/" + relativePath
    };
    for (const auto& p : searchPaths) {
        if (std::filesystem::exists(p)) {
            return p;
        }
    }
    return relativePath; // Fallback
}

} // anonymous namespace

Application::Application(int width, int height, const std::string& title)
    : m_windowWidth(width),
      m_windowHeight(height),
      m_windowTitle(title),
      m_window(nullptr),
      m_camera(glm::vec3(0.0f, 0.45f, 0.0f), 1.55f, 45.0f, 18.0f),
      m_noise3DTexture(0),
      m_shadowMap(2048),
      m_screenVao(0),
      m_isLeftDragging(false),
      m_isRightDragging(false),
      m_lastMouseX(0.0),
      m_lastMouseY(0.0),
      m_fps(60.0f),
      m_frameTimeMs(16.6f),
      m_frameCount(0),
      m_fpsTimer(0.0f)
{}

Application::~Application() {
    if (m_window) {
        if (m_screenVao) glDeleteVertexArrays(1, &m_screenVao);
        if (m_noise3DTexture) glDeleteTextures(1, &m_noise3DTexture);

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(m_window);
        glfwTerminate();
    }
}

bool Application::init() {
    // 1. Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "[Application ERROR] Failed to initialize GLFW!" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 0); // No MSAA needed for volumetric ray marching

    m_window = glfwCreateWindow(m_windowWidth, m_windowHeight, m_windowTitle.c_str(), nullptr, nullptr);
    if (!m_window) {
        std::cerr << "[Application ERROR] Failed to create GLFW window!" << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // Enable V-Sync for smooth 60 FPS presentation

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetCursorPosCallback(m_window, cursorPosCallback);
    glfwSetScrollCallback(m_window, scrollCallback);

    // 2. Initialize GLAD (OpenGL 4.5 Core)
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "[Application ERROR] Failed to initialize GLAD!" << std::endl;
        return false;
    }

    std::cout << "[OpenGL] Vendor:   " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "[OpenGL] Renderer: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "[OpenGL] Version:  " << glGetString(GL_VERSION) << std::endl;

    // 3. Initialize Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // Customize ImGui style for high-contrast classroom presentation
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.92f);
    style.Colors[ImGuiCol_Header]   = ImVec4(0.20f, 0.28f, 0.40f, 0.85f);
    style.Colors[ImGuiCol_Button]   = ImVec4(0.24f, 0.35f, 0.52f, 0.80f);

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 450");

    // 4. Create Screen VAO for Fullscreen Triangle
    glGenVertexArrays(1, &m_screenVao);

    // 5. Load Shaders
    std::string vVolPath = resolvePath("shaders/volumetric.vert");
    std::string fVolPath = resolvePath("shaders/volumetric.frag");
    if (!m_volumetricShader.loadFromFiles(vVolPath, fVolPath)) {
        std::cerr << "[Application ERROR] Failed to load Volumetric Shader!" << std::endl;
        return false;
    }

    std::string vSolidPath = resolvePath("shaders/solid_scene.vert");
    std::string fSolidPath = resolvePath("shaders/solid_scene.frag");
    if (!m_solidShader.loadFromFiles(vSolidPath, fSolidPath)) {
        std::cerr << "[Application ERROR] Failed to load Solid Scene Shader!" << std::endl;
        return false;
    }

    std::string vShadowPath = resolvePath("shaders/shadow_map.vert");
    std::string fShadowPath = resolvePath("shaders/shadow_map.frag");
    if (!m_shadowShader.loadFromFiles(vShadowPath, fShadowPath)) {
        std::cerr << "[Application ERROR] Failed to load Shadow Map Shader!" << std::endl;
        return false;
    }

    std::string vCompPath = resolvePath("shaders/composite.vert");
    std::string fCompPath = resolvePath("shaders/composite.frag");
    if (!m_compositeShader.loadFromFiles(vCompPath, fCompPath)) {
        std::cerr << "[Application ERROR] Failed to load Composite Shader!" << std::endl;
        return false;
    }

    // 6. Load Assets
    std::string volAssetDir = resolvePath("assets/volumes/SmallCampfireVDB/");
    if (!m_volumeAsset.loadFromDirectory(volAssetDir)) {
        std::cerr << "[Application WARNING] Could not load volume frames from " << volAssetDir << std::endl;
    }

    m_noise3DTexture = NoiseGenerator::generateTileable3DNoise(128);
    m_geometry.init();
    m_shadowMap.init();

    // 7. Create Framebuffers
    resizeFramebuffers(m_windowWidth, m_windowHeight);

    std::cout << "[Application] Initialization completed successfully." << std::endl;
    return true;
}

void Application::resizeFramebuffers(int width, int height) {
    if (width <= 0 || height <= 0) return;
    m_windowWidth = width;
    m_windowHeight = height;

    // Full-resolution scene FBO with depth buffer
    m_sceneFBO = std::make_unique<Framebuffer>(m_windowWidth, m_windowHeight, true, true);
    // Full-resolution volumetric FBO
    m_volFullFBO = std::make_unique<Framebuffer>(m_windowWidth, m_windowHeight, false, true);
    // Half-resolution volumetric FBO (0.5x width and height) for downsampled sampling mode
    m_volHalfFBO = std::make_unique<Framebuffer>(std::max(m_windowWidth / 2, 1), std::max(m_windowHeight / 2, 1), false, true);
}

void Application::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app) {
        app->resizeFramebuffers(width, height);
    }
}

void Application::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (!app || ImGui::GetIO().WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        app->m_isLeftDragging = (action == GLFW_PRESS);
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        app->m_isRightDragging = (action == GLFW_PRESS);
    }

    if (action == GLFW_PRESS) {
        glfwGetCursorPos(window, &app->m_lastMouseX, &app->m_lastMouseY);
    }
}

void Application::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (!app || ImGui::GetIO().WantCaptureMouse) return;

    if (app->m_isLeftDragging || app->m_isRightDragging) {
        float deltaX = static_cast<float>(xpos - app->m_lastMouseX);
        float deltaY = static_cast<float>(ypos - app->m_lastMouseY);
        app->m_camera.processMouseMovement(deltaX, deltaY, app->m_isLeftDragging, app->m_isRightDragging);
        app->m_lastMouseX = xpos;
        app->m_lastMouseY = ypos;
    }
}

void Application::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (!app || ImGui::GetIO().WantCaptureMouse) return;

    app->m_camera.processMouseScroll(static_cast<float>(yoffset));
}

void Application::processInput(float deltaTime) {
    if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(m_window, true);
    }
}

void Application::update(float deltaTime) {
    // Automatic light orbit animation if toggled
    if (m_settings.orbitLight) {
        float speed = 0.8f;
        float radius = 2.4f;
        float angle = static_cast<float>(glfwGetTime()) * speed;
        m_settings.lightPos.x = std::cos(angle) * radius;
        m_settings.lightPos.z = std::sin(angle) * radius;
    }

    // Update volume playback
    if (m_settings.type == SceneType::CampfireVDB) {
        if (m_settings.isPlaying) {
            m_volumeAsset.update(deltaTime, m_settings.playbackSpeed, true);
        } else {
            m_volumeAsset.setManualFrame(m_settings.manualFrame);
        }
    }

    // Performance statistics
    m_frameCount++;
    m_fpsTimer += deltaTime;
    if (m_fpsTimer >= 0.5f) {
        m_fps = static_cast<float>(m_frameCount) / m_fpsTimer;
        m_frameTimeMs = (m_fpsTimer / static_cast<float>(m_frameCount)) * 1000.0f;
        m_frameCount = 0;
        m_fpsTimer = 0.0f;
    }
}

void Application::renderPassShadowMap() {
    // Only required for Scene 3 (Spotlight God Rays)
    if (m_settings.type != SceneType::GodRaysGeometry) return;

    m_shadowMap.calculateLightSpaceMatrix(
        m_settings.lightPos,
        m_settings.spotlightTarget,
        m_settings.spotlightCutoff,
        0.5f,
        15.0f
    );

    m_shadowMap.bind();
    glEnable(GL_DEPTH_TEST);

    m_shadowShader.use();
    m_shadowShader.setMat4("u_LightSpaceMatrix", m_shadowMap.getLightSpaceMatrix());

    // Floor model matrix
    glm::mat4 modelFloor = glm::mat4(1.0f);
    m_shadowShader.setMat4("u_Model", modelFloor);
    m_geometry.renderFloor();

    // 6 Columns (2 rows of 3 forming a colonnade)
    const glm::vec2 colPositions[6] = {
        {-1.4f, -1.6f}, { 1.4f, -1.6f},
        {-1.4f,  0.0f}, { 1.4f,  0.0f},
        {-1.4f,  1.6f}, { 1.4f,  1.6f}
    };
    for (int i = 0; i < 6; ++i) {
        glm::mat4 modelCol = glm::translate(glm::mat4(1.0f), glm::vec3(colPositions[i].x, 0.0f, colPositions[i].y));
        m_shadowShader.setMat4("u_Model", modelCol);
        m_geometry.renderColumns();
    }

    // 2 Longitudinal architraves resting on top of the columns
    for (float sideX : {-1.4f, 1.4f}) {
        glm::mat4 modelBeam = glm::translate(glm::mat4(1.0f), glm::vec3(sideX, 2.2f, 0.0f));
        modelBeam = glm::scale(modelBeam, glm::vec3(0.36f, 0.25f, 3.8f));
        m_shadowShader.setMat4("u_Model", modelBeam);
        m_geometry.renderBox();
    }

    // 5 Transverse pergola rafters creating crisp light shafts (God Rays)
    for (float beamZ : {-1.6f, -0.8f, 0.0f, 0.8f, 1.6f}) {
        glm::mat4 modelRafter = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.45f, beamZ));
        modelRafter = glm::scale(modelRafter, glm::vec3(3.4f, 0.22f, 0.26f));
        m_shadowShader.setMat4("u_Model", modelRafter);
        m_geometry.renderBox();
    }

    m_shadowMap.unbind();
}

void Application::renderPassSolidScene() {
    m_sceneFBO->bind();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Clear background: alpha = 0.0 signals unoccluded background for composite pass
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Setup camera matrices for solid geometry
    float aspect = static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight);
    m_solidShader.use();
    m_solidShader.setInt("u_SceneMode", static_cast<int>(m_settings.type));
    m_solidShader.setMat4("u_View", m_camera.getViewMatrix());
    m_solidShader.setMat4("u_Projection", m_camera.getProjectionMatrix(aspect));
    m_solidShader.setMat4("u_LightSpaceMatrix", m_shadowMap.getLightSpaceMatrix());

    m_solidShader.setVec3("u_LightPos", m_settings.lightPos);
    m_solidShader.setVec3("u_LightColor", m_settings.lightColor);
    glm::vec3 spotDir = glm::normalize(m_settings.spotlightTarget - m_settings.lightPos);
    m_solidShader.setVec3("u_SpotlightDir", spotDir);
    m_solidShader.setFloat("u_SpotlightCutoff", std::cos(glm::radians(m_settings.spotlightCutoff)));
    m_solidShader.setVec3("u_CameraPos", m_camera.getPosition());

    // Dynamic fire point light (flickers subtly with time for realistic glow on the floor)
    float timeVal = static_cast<float>(glfwGetTime());
    float flicker = 1.0f + 0.08f * std::sin(timeVal * 12.0f) + 0.05f * std::sin(timeVal * 23.0f);
    m_solidShader.setVec3("u_FireLightPos", glm::vec3(0.0f, 0.22f, 0.0f));
    m_solidShader.setVec3("u_FireLightColor", glm::vec3(1.2f, 0.52f, 0.08f));
    m_solidShader.setFloat("u_FireLightIntensity", 3.2f * flicker);

    // Bind shadow map to texture unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap.getDepthTexture());
    m_solidShader.setInt("u_ShadowMapTex", 0);

    // 1. Render Floor Plane if enabled (visible in all scenes as ground reference)
    if (m_settings.showGround) {
        m_solidShader.setBool("u_IsFloor", true);
        glm::mat4 modelFloor = glm::mat4(1.0f);
        m_solidShader.setMat4("u_Model", modelFloor);
        m_solidShader.setVec3("u_ObjectColor", glm::vec3(0.18f, 0.20f, 0.22f));
        m_geometry.renderFloor();
    }

    // 2. Render Colonnade & Pergola (Scene 3: God Rays Geometry)
    if (m_settings.type == SceneType::GodRaysGeometry) {
        m_solidShader.setBool("u_IsFloor", false);
        const glm::vec2 colPositions[6] = {
            {-1.4f, -1.6f}, { 1.4f, -1.6f},
            {-1.4f,  0.0f}, { 1.4f,  0.0f},
            {-1.4f,  1.6f}, { 1.4f,  1.6f}
        };
        m_solidShader.setVec3("u_ObjectColor", glm::vec3(0.76f, 0.72f, 0.66f));
        for (int i = 0; i < 6; ++i) {
            glm::mat4 modelCol = glm::translate(glm::mat4(1.0f), glm::vec3(colPositions[i].x, 0.0f, colPositions[i].y));
            m_solidShader.setMat4("u_Model", modelCol);
            m_geometry.renderColumns();
        }

        // Architraves & pergola rafters (slightly darker limestone)
        m_solidShader.setVec3("u_ObjectColor", glm::vec3(0.58f, 0.55f, 0.50f));
        for (float sideX : {-1.4f, 1.4f}) {
            glm::mat4 modelBeam = glm::translate(glm::mat4(1.0f), glm::vec3(sideX, 2.2f, 0.0f));
            modelBeam = glm::scale(modelBeam, glm::vec3(0.36f, 0.25f, 3.8f));
            m_solidShader.setMat4("u_Model", modelBeam);
            m_geometry.renderBox();
        }
        for (float beamZ : {-1.6f, -0.8f, 0.0f, 0.8f, 1.6f}) {
            glm::mat4 modelRafter = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.45f, beamZ));
            modelRafter = glm::scale(modelRafter, glm::vec3(3.4f, 0.22f, 0.26f));
            m_solidShader.setMat4("u_Model", modelRafter);
            m_geometry.renderBox();
        }
    }

    m_sceneFBO->unbind();
}

void Application::renderPassVolumetric() {
    // Select active FBO: Half-Res downsampling or Full-Res native
    Framebuffer* targetFBO = m_settings.halfRes ? m_volHalfFBO.get() : m_volFullFBO.get();
    targetFBO->bind();

    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_volumetricShader.use();

    // Set Camera & Matrix Uniforms
    float aspect = static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight);
    m_volumetricShader.setMat4("u_InvView", m_camera.getInverseViewMatrix());
    m_volumetricShader.setMat4("u_InvProjection", m_camera.getInverseProjectionMatrix(aspect));
    m_volumetricShader.setVec3("u_CameraPos", m_camera.getPosition());
    m_volumetricShader.setVec2("u_ScreenResolution", static_cast<float>(targetFBO->getWidth()), static_cast<float>(targetFBO->getHeight()));
    m_volumetricShader.setFloat("u_NearPlane", m_camera.getNearPlane());
    m_volumetricShader.setFloat("u_FarPlane", m_camera.getFarPlane());
    m_volumetricShader.setFloat("u_Time", static_cast<float>(glfwGetTime()));

    // Scene & Bounding Box
    m_volumetricShader.setInt("u_SceneMode", static_cast<int>(m_settings.type));
    m_volumetricShader.setVec3("u_BoxMin", m_settings.boxMin);
    m_volumetricShader.setVec3("u_BoxMax", m_settings.boxMax);

    // Ray Marching Parameters
    m_volumetricShader.setInt("u_MaxSteps", m_settings.maxSteps);
    m_volumetricShader.setFloat("u_StepSize", m_settings.stepSize);
    m_volumetricShader.setBool("u_EnableJitter", m_settings.enableJitter);
    m_volumetricShader.setBool("u_EnableSelfShadow", m_settings.enableSelfShadow);
    m_volumetricShader.setInt("u_ShadowSteps", m_settings.shadowSteps);
    m_volumetricShader.setInt("u_DebugMode", m_settings.debugMode);

    // Optical & Physical Coefficients
    m_volumetricShader.setFloat("u_SigmaA", m_settings.sigmaA);
    m_volumetricShader.setFloat("u_SigmaS", m_settings.sigmaS);
    m_volumetricShader.setFloat("u_HenyeyG", m_settings.henyeyG);
    m_volumetricShader.setFloat("u_EmissionIntensity", m_settings.emissionIntensity);
    m_volumetricShader.setFloat("u_DensityMultiplier", m_settings.densityMultiplier);

    // Lighting Uniforms
    m_volumetricShader.setVec3("u_LightPos", m_settings.lightPos);
    m_volumetricShader.setVec3("u_LightColor", m_settings.lightColor);
    m_volumetricShader.setFloat("u_LightIntensity", m_settings.lightIntensity);
    m_volumetricShader.setMat4("u_LightSpaceMatrix", m_shadowMap.getLightSpaceMatrix());
    glm::vec3 spotDir = glm::normalize(m_settings.spotlightTarget - m_settings.lightPos);
    m_volumetricShader.setVec3("u_SpotlightDir", spotDir);
    m_volumetricShader.setFloat("u_SpotlightCutoff", std::cos(glm::radians(m_settings.spotlightCutoff)));

    // Bind Textures to Units 0 to 4
    // Unit 0: Campfire current frame
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, m_volumeAsset.getCurrentTexture());
    m_volumetricShader.setInt("u_VolumeTexCurrent", 0);

    // Unit 1: Campfire next frame
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, m_volumeAsset.getNextTexture());
    m_volumetricShader.setInt("u_VolumeTexNext", 1);
    m_volumetricShader.setFloat("u_FrameBlend", m_volumeAsset.getFrameBlend());

    // Unit 2: 3D Tileable Noise
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, m_noise3DTexture);
    m_volumetricShader.setInt("u_NoiseTex3D", 2);

    // Unit 3: Solid Scene Depth Buffer
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_sceneFBO->getDepthTexture());
    m_volumetricShader.setInt("u_SceneDepthTex", 3);

    // Unit 4: Spotlight Shadow Map
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap.getDepthTexture());
    m_volumetricShader.setInt("u_ShadowMapTex", 4);

    // Draw fullscreen triangle
    glBindVertexArray(m_screenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    targetFBO->unbind();
}

void Application::renderPassComposite() {
    // Bind default backbuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_windowWidth, m_windowHeight);
    glClear(GL_COLOR_BUFFER_BIT);

    m_compositeShader.use();
    m_compositeShader.setInt("u_DebugMode", m_settings.debugMode);
    m_compositeShader.setFloat("u_Exposure", m_settings.exposure);
    m_compositeShader.setInt("u_SceneMode", static_cast<int>(m_settings.type));

    // Unit 0: Solid Scene Color
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneFBO->getColorTexture());
    m_compositeShader.setInt("u_SceneColorTex", 0);

    // Unit 1: Volumetric Buffer (Half-Res or Full-Res)
    glActiveTexture(GL_TEXTURE1);
    GLuint volTex = m_settings.halfRes ? m_volHalfFBO->getColorTexture() : m_volFullFBO->getColorTexture();
    glBindTexture(GL_TEXTURE_2D, volTex);
    m_compositeShader.setInt("u_VolumetricColorTex", 1);

    // Draw fullscreen triangle
    glBindVertexArray(m_screenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void Application::run() {
    float lastFrameTime = static_cast<float>(glfwGetTime());

    while (!glfwWindowShouldClose(m_window)) {
        float currentFrameTime = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        glfwPollEvents();
        processInput(deltaTime);
        update(deltaTime);

        // 1. Render Passes
        renderPassShadowMap();
        renderPassSolidScene();
        renderPassVolumetric();
        renderPassComposite();

        // 2. ImGui UI Render Pass
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        m_ui.render(m_settings, m_camera, m_fps, m_frameTimeMs);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(m_window);
    }
}

} // namespace vrm
