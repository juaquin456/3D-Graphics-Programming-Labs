#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <limits>
#include <iomanip>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "common/Camera.h"
#include "common/Shader.h"

#include "SceneTypes.h"
#include "EmbreePathTracer.h"
#include "OptixPathTracer.h"
#include "scene.h"
// ─────────────────────────────────────────────────────────────
//  Camera raygen parameter constructor
// ─────────────────────────────────────────────────────────────
inline RayGenParams makeRayGenParams(
    const glm::vec3& eye,
    const glm::vec3& lookAt,
    const glm::vec3& up,
    float fovYDegrees,
    float aspectRatio,
    uint32_t frameIndex = 0
) {
    float theta = glm::radians(fovYDegrees);
    float h = tanf(theta / 2.0f);
    float viewportHeight = 2.0f * h;
    float viewportWidth = aspectRatio * viewportHeight;

    glm::vec3 w = glm::normalize(eye - lookAt);
    glm::vec3 u = glm::normalize(glm::cross(up, w));
    glm::vec3 v = glm::cross(w, u);

    glm::vec3 horizontal = viewportWidth * u;
    glm::vec3 vertical = viewportHeight * v;
    glm::vec3 lowerLeftCorner = eye - horizontal * 0.5f - vertical * 0.5f - w;

    RayGenParams params{};
    params.origin          = eye;
    params.lowerLeftCorner = lowerLeftCorner;
    params.horizontal      = horizontal;
    params.vertical        = vertical;
    params.frameIndex      = frameIndex;

    return params;
}

// ─────────────────────────────────────────────────────────────
//  Global state
// ─────────────────────────────────────────────────────────────
int SCR_WIDTH  = 1280;
int SCR_HEIGHT = 720;

Camera     g_cam;
bool       g_showGui    = true;
bool       g_isDragging = false;
double     g_lastMouseX = 0, g_lastMouseY = 0;

std::vector<PathTriangle> g_worldTriangles;
std::vector<PathMaterial> g_worldMaterials;
std::vector<PathMaterial> g_baseMaterials;
glm::vec3 g_sceneMin{0.0f}, g_sceneMax{0.0f};

EmbreePathTracer g_embree;
OptixPathTracer  g_optix;
bool g_embreeOK = false;
bool g_optixOK  = false;

enum class Backend { Embree, OptiX };
Backend g_backend = Backend::OptiX;

// Per-backend render dimensions
int g_renderW = 640, g_renderH = 360;

// OpenGL resources
GLuint g_tex     = 0;
GLuint g_pbo     = 0;
GLuint g_quadVAO = 0, g_quadVBO = 0;

// CPU framebuffer for Embree
std::vector<unsigned char> g_cpuFB;

// Scene list
struct SceneEntry { const char* name; const char* path; };
const SceneEntry SCENES[] = {
    {"Teapot",        "../../models/teapot.obj"},
    {"Stanford Bunny", "../../models/stanford-bunny.obj"},
    {"Fireplace Room", "../../scenes/fireplace_room/fireplace_room.obj"},
    {"LPS Head",       "../../scenes/lpshead/head.OBJ"},
    {"Custom",         ""},
};
int  g_selectedScene = 0; // Stanford Bunny
char g_customPath[512] = "../../models/teapot.obj";

PTSettings g_ptSettings;

struct PerfStats {
    double lastRenderMs = 0.0;
    double avgFPS       = 0.0;
    uint32_t samples    = 0;
};
PerfStats g_embreeStats, g_optixStats;

const char* const PRESET_NAMES[] = {
    "Scene (from OBJ)",
    "Diffuse – White",
    "Diffuse – Red",
    "Diffuse – Blue",
    "Mirror Specular",
    "Gold Mirror",
    "Glass (IOR 1.5)",
    "Diamond (IOR 2.4)",
};
int g_overrideMat = 0;

static std::string loadPTX(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return "";
    std::streamsize sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::string s(sz, '\0');
    f.read(s.data(), sz);
    return s;
}

PathMaterial presetMaterial(int idx) {
    PathMaterial m{};
    switch (idx) {
        case 1: m.type=MaterialType::Diffuse;    m.albedo={0.9f,0.9f,0.9f}; break;
        case 2: m.type=MaterialType::Diffuse;    m.albedo={0.8f,0.1f,0.1f}; break;
        case 3: m.type=MaterialType::Diffuse;    m.albedo={0.1f,0.2f,0.8f}; break;
        case 4: m.type=MaterialType::Specular;   m.albedo={0.95f,0.95f,0.95f}; break;
        case 5: m.type=MaterialType::Specular;   m.albedo={0.83f,0.68f,0.22f}; break;
        case 6: m.type=MaterialType::Dielectric; m.albedo={1.0f,1.0f,1.0f}; m.ior=1.5f; break;
        case 7: m.type=MaterialType::Dielectric; m.albedo={1.0f,1.0f,1.0f}; m.ior=2.4f; break;
        default: m.type=MaterialType::Diffuse;   m.albedo={0.8f,0.8f,0.8f}; break;
    }
    return m;
}

void rebuildScene() {
    if (g_worldTriangles.empty()) {
        g_sceneMin = {-1.0f, -1.0f, -1.0f};
        g_sceneMax = { 1.0f,  1.0f,  1.0f};
        return;
    }

    g_worldMaterials = g_baseMaterials;
    if (g_overrideMat > 0) {
        PathMaterial overrideMat = presetMaterial(g_overrideMat);
        for (auto& mat : g_worldMaterials) {
            mat = overrideMat;
        }
    }

    g_sceneMin = glm::vec3(std::numeric_limits<float>::infinity());
    g_sceneMax = glm::vec3(-std::numeric_limits<float>::infinity());
    for (const auto& t : g_worldTriangles) {
        g_sceneMin = glm::min(g_sceneMin, glm::min(t.v0, glm::min(t.v1, t.v2)));
        g_sceneMax = glm::max(g_sceneMax, glm::max(t.v0, glm::max(t.v1, t.v2)));
    }

    if (g_embreeOK) g_embree.uploadScene(g_worldTriangles, g_worldMaterials);
    if (g_optixOK)  g_optix.uploadScene(g_worldTriangles, g_worldMaterials);

    std::cout << "[Scene] " << g_worldTriangles.size() << " tris, "
              << g_worldMaterials.size() << " materials (Override: " << g_overrideMat << ")." << std::endl;
}

void frameCamera() {
    glm::vec3 center = (g_sceneMin + g_sceneMax) * 0.5f;
    float radius = glm::length(g_sceneMax - g_sceneMin) * 0.5f;
    if (radius < 1e-4f) radius = 1.0f;
    float dist = (radius / std::tan(glm::radians(g_cam.fov_deg * 0.5f))) * 1.25f;

    g_cam.target   = center;
    g_cam.position = center + glm::vec3(0.0f, radius * 0.1f, dist);
    g_cam.near_z   = std::max(0.01f, dist * 0.01f);
    g_cam.far_z    = dist * 15.0f;
    g_cam.speed    = radius * 0.5f;

    if (g_embreeOK) g_embree.resetAccumulation();
    if (g_optixOK)  g_optix.resetAccumulation();
}

void loadScene(const std::string& path) {
    PathSceneData sceneData = loadPathScene(path) ;//createProceduralCornellBox();

    if (!sceneData.triangles.empty()) {
        g_worldTriangles = std::move(sceneData.triangles);
        g_baseMaterials  = std::move(sceneData.materials);

        rebuildScene();
        frameCamera();
    } else {
        std::cerr << "[App] Error al cargar la escena: " << path << std::endl;
    }
}

void setupRenderBuffers(int w, int h) {
    if (g_optixOK) g_optix.unregisterPBO();

    if (!g_pbo) glGenBuffers(1, &g_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, g_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, w * h * 4, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    g_cpuFB.assign(w * h * 4, 0);

    if (g_optixOK) g_optix.registerPBO(g_pbo);
    if (g_optixOK) g_optix.resize(w, h);

    if (g_embreeOK) g_embree.resetAccumulation();
}

void applyResolution(int idx) {
    static const int W[] = {320, 480, 640, 960, 1280};
    static const int H[] = {180, 270, 360, 540, 720};
    g_renderW = W[idx];
    g_renderH = H[idx];
    setupRenderBuffers(g_renderW, g_renderH);
}

// ─────────────────────────────────────────────────────────────
//  Main render call
// ─────────────────────────────────────────────────────────────
void doRender() {
    RayGenParams camera = makeRayGenParams(
        g_cam.position, g_cam.target, g_cam.up,
        g_cam.fov_deg, float(g_renderW) / float(g_renderH),
        (g_backend == Backend::OptiX) ? (g_optixOK ? g_optix.sampleCount() : 0)
                                      : g_embree.sampleCount()
    );

    if (g_backend == Backend::OptiX && g_optixOK) {
        double ms = g_optix.render(camera, g_ptSettings);
        g_optixStats.lastRenderMs = ms;
        g_optixStats.samples      = g_optix.sampleCount();
        g_optixStats.avgFPS       = (ms > 0.001) ? 1000.0 / ms : 0.0;

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, g_pbo);
        glBindTexture(GL_TEXTURE_2D, g_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_renderW, g_renderH,
                        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);

    } else if (g_backend == Backend::Embree && g_embreeOK) {
        double ms = g_embree.render(camera, g_ptSettings, g_cpuFB.data(), g_renderW, g_renderH);
        g_embreeStats.lastRenderMs = ms;
        g_embreeStats.samples      = g_embree.sampleCount();
        g_embreeStats.avgFPS       = (ms > 0.001) ? 1000.0 / ms : 0.0;

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, g_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_renderW, g_renderH,
                        GL_RGBA, GL_UNSIGNED_BYTE, g_cpuFB.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

// ─────────────────────────────────────────────────────────────
//  GLFW callbacks
// ─────────────────────────────────────────────────────────────
void framebufferSizeCB(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h);
    SCR_WIDTH  = w; SCR_HEIGHT = h;
    g_cam.aspect_ratio = float(w) / float(h);
}

void scrollCB(GLFWwindow*, double, double dy) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
    glm::vec3 dir = g_cam.position - g_cam.target;
    float d = glm::length(dir) * (1.0f - 0.15f * float(dy));
    d = std::max(0.05f, d);
    g_cam.position = g_cam.target + glm::normalize(dir) * d;
    if (g_embreeOK) g_embree.resetAccumulation();
    if (g_optixOK)  g_optix.resetAccumulation();
}

void mouseButtonCB(GLFWwindow* w, int btn, int act, int) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (btn == GLFW_MOUSE_BUTTON_LEFT) {
        g_isDragging = (act == GLFW_PRESS);
        if (g_isDragging) glfwGetCursorPos(w, &g_lastMouseX, &g_lastMouseY);
    }
}

void cursorPosCB(GLFWwindow*, double x, double y) {
    if (!g_isDragging || ImGui::GetIO().WantCaptureMouse) return;
    float dx = float(x - g_lastMouseX), dy = float(y - g_lastMouseY);
    g_lastMouseX = x; g_lastMouseY = y;
    g_cam.rotateOrbit(dx, dy, 0.005f);
    if (g_embreeOK) g_embree.resetAccumulation();
    if (g_optixOK)  g_optix.resetAccumulation();
}

void keyboardCB(GLFWwindow* w, int key, int, int action, int) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, true);
        if (key == GLFW_KEY_H) g_showGui = !g_showGui;
        if (key == GLFW_KEY_F) { frameCamera(); }
        if (key == GLFW_KEY_G) {
            g_backend = (g_backend == Backend::OptiX) ? Backend::Embree : Backend::OptiX;
            if (g_embreeOK) g_embree.resetAccumulation();
            if (g_optixOK)  g_optix.resetAccumulation();
        }
    }
}

void processInput(GLFWwindow* w, float dt) {
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    bool moved = false;
    if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS) { g_cam.moveForward(dt); moved=true; }
    if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS) { g_cam.moveBackward(dt); moved=true; }
    if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS) { g_cam.moveLeft(dt); moved=true; }
    if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) { g_cam.moveRight(dt); moved=true; }
    if (moved) {
        if (g_embreeOK) g_embree.resetAccumulation();
        if (g_optixOK)  g_optix.resetAccumulation();
    }
}

// ─────────────────────────────────────────────────────────────
//  ImGui
// ─────────────────────────────────────────────────────────────
void setupScreenQuad() {
    float v[] = {
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
    };
    glGenVertexArrays(1, &g_quadVAO);
    glGenBuffers(1, &g_quadVBO);
    glBindVertexArray(g_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
}

void renderGui() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({480, 800}, ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Lab 16: BSDF Path Tracer", &g_showGui)) {
        if (ImGui::CollapsingHeader("Backend (G to toggle)", ImGuiTreeNodeFlags_DefaultOpen)) {
            int b = (int)g_backend;
            bool changedB = false;
            if (ImGui::RadioButton("GPU – NVIDIA OptiX (RT Cores)", &b, 1)) changedB = true;
            if (ImGui::RadioButton("CPU – Intel Embree (BVH)",      &b, 0)) changedB = true;
            if (changedB) {
                g_backend = (Backend)b;
                if (g_embreeOK) g_embree.resetAccumulation();
                if (g_optixOK)  g_optix.resetAccumulation();
            }
            ImGui::Separator();
            ImGui::Text("GPU Device: %s", g_optixOK ? OptixPathTracer::deviceName().c_str() : "N/A");
            ImGui::Text("CPU: Intel Embree 4 + OpenMP");
        }

        if (ImGui::CollapsingHeader("Performance: FPS Comparison", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto fpsColor = [](double fps) -> ImVec4 {
                if (fps >= 5.0)  return {0.2f, 1.0f, 0.4f, 1.0f};
                if (fps >= 0.5)  return {1.0f, 0.8f, 0.2f, 1.0f};
                return {1.0f, 0.3f, 0.3f, 1.0f};
            };
            ImGui::TextColored(fpsColor(g_optixStats.avgFPS),
                "GPU (OptiX):  %8.2f FPS  (%8.4f ms/frame)  [%u spp]",
                g_optixStats.avgFPS, g_optixStats.lastRenderMs, g_optixStats.samples);
            ImGui::TextColored(fpsColor(g_embreeStats.avgFPS),
                "CPU (Embree): %8.2f FPS  (%8.4f ms/frame)  [%u spp]",
                g_embreeStats.avgFPS, g_embreeStats.lastRenderMs, g_embreeStats.samples);
            if (g_embreeStats.lastRenderMs > 0.0 && g_optixStats.lastRenderMs > 0.0) {
                double speedup = g_embreeStats.lastRenderMs / g_optixStats.lastRenderMs;
                ImGui::TextColored({0.8f, 0.5f, 1.0f, 1.0f},
                    "  GPU Speedup: %.1fx faster than CPU", speedup);
            }
            ImGui::Text("Resolution: %d x %d  |  %d triangles", g_renderW, g_renderH,
                        (int)g_worldTriangles.size());
        }

        if (ImGui::CollapsingHeader("Path Tracer Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool resetAcc = false;
            resetAcc |= ImGui::SliderInt("Max Ray Depth", &g_ptSettings.maxDepth, 1, 16);
            resetAcc |= ImGui::Checkbox("Progressive Accumulation", &g_ptSettings.accumulate);

            if (!g_ptSettings.accumulate || ImGui::Button("Reset Accumulation")) {
                if (g_embreeOK) g_embree.resetAccumulation();
                if (g_optixOK)  g_optix.resetAccumulation();
            }
            if (resetAcc) {
                if (g_embreeOK) g_embree.resetAccumulation();
                if (g_optixOK)  g_optix.resetAccumulation();
            }

            uint32_t spActive = (g_backend == Backend::OptiX && g_optixOK)
                ? g_optix.sampleCount() : g_embree.sampleCount();
            ImGui::Text("Accumulated samples: %u", spActive);
        }

        if (ImGui::CollapsingHeader("Resolution", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* resOpts[] = {"320×180","480×270","640×360","960×540","1280×720"};
            static int resIdx = 2;
            if (ImGui::Combo("Resolution", &resIdx, resOpts, IM_ARRAYSIZE(resOpts))) {
                applyResolution(resIdx);
            }
        }

        if (ImGui::CollapsingHeader("Material Override", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Combo("Material", &g_overrideMat, PRESET_NAMES, IM_ARRAYSIZE(PRESET_NAMES))) {
                rebuildScene();
            }
            ImGui::TextDisabled("Override applies ALL scene objects with selected BSDF.");
        }

        if (ImGui::CollapsingHeader("Scene & Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* sceneNames[5];
            for (int i = 0; i < 5; ++i) sceneNames[i] = SCENES[i].name;
            if (ImGui::Combo("Scene", &g_selectedScene, sceneNames, 5)) {
                if (g_selectedScene < 4) loadScene(SCENES[g_selectedScene].path);
            }
            if (g_selectedScene == 4) {
                ImGui::InputText("OBJ Path", g_customPath, sizeof(g_customPath));
                if (ImGui::Button("Load")) loadScene(g_customPath);
            }
            if (ImGui::Button("Frame Camera (F)")) frameCamera();
            ImGui::SameLine();
            if (ImGui::SliderFloat("FOV", &g_cam.fov_deg, 15.0f, 90.0f)) {
                if (g_embreeOK) g_embree.resetAccumulation();
                if (g_optixOK)  g_optix.resetAccumulation();
            }
            ImGui::Text("Cam: (%.2f, %.2f, %.2f)", g_cam.position.x, g_cam.position.y, g_cam.position.z);
        }

        if (ImGui::CollapsingHeader("Controls")) {
            ImGui::BulletText("Left drag: Orbit camera");
            ImGui::BulletText("Scroll: Zoom");
            ImGui::BulletText("WASD: Pan camera");
            ImGui::BulletText("G: Toggle GPU / CPU backend");
            ImGui::BulletText("F: Frame camera to scene");
            ImGui::BulletText("H: Toggle this GUI");
        }
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ─────────────────────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT,
        "Lab 16: BSDF Path Tracer (Embree CPU / OptiX GPU)", nullptr, nullptr);
    if (!win) { std::cerr << "GLFW window failed\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(0);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "GLAD init failed\n"; return -1;
    }
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glfwSetFramebufferSizeCallback(win, framebufferSizeCB);
    glfwSetScrollCallback(win, scrollCB);
    glfwSetMouseButtonCallback(win, mouseButtonCB);
    glfwSetCursorPosCallback(win, cursorPosCB);
    glfwSetKeyCallback(win, keyboardCB);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    setupScreenQuad();

    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    Shader screenShader("../../shaders/screen_quad.vert", "../../shaders/screen_quad.frag");

    // ── Init Embree ──
    g_embreeOK = EmbreePathTracer::isAvailable() && g_embree.init();
    std::cout << "[Embree] " << (g_embreeOK ? "OK" : "FAILED") << std::endl;

    // ── Init OptiX ──
    std::string ptxPath = "optix_kernels.ptx";
    if (argc > 1 && std::string(argv[1]).find(".ptx") != std::string::npos) {
        ptxPath = argv[1];
    }
    std::string ptx = loadPTX(ptxPath);
    if (ptx.empty()) {
        std::cerr << "[OptiX] PTX not found at " << ptxPath
                  << " – GPU backend will be unavailable." << std::endl;
        g_optixOK = false;
    } else {
        g_optixOK = OptixPathTracer::isAvailable() && g_optix.init(ptx);
        std::cout << "[OptiX] " << (g_optixOK ? "OK – " + OptixPathTracer::deviceName() : "FAILED") << std::endl;
    }

    if (!g_embreeOK && !g_optixOK) {
        std::cerr << "[App] Neither Embree nor OptiX available. Exiting.\n";
        return -1;
    }

    if (g_optixOK)       g_backend = Backend::OptiX;
    else if (g_embreeOK) g_backend = Backend::Embree;

    // ── Setup buffers ──
    setupRenderBuffers(g_renderW, g_renderH);

    // ── Load scene ──
    std::string initScene = SCENES[g_selectedScene].path;
    if (argc > 1 && std::string(argv[1]).find(".obj") != std::string::npos) {
        initScene = argv[1];
    }
    loadScene(initScene);

    float lastTime = float(glfwGetTime());

    // ── Main loop ──
    while (!glfwWindowShouldClose(win)) {
        float now = float(glfwGetTime());
        float dt  = now - lastTime;
        lastTime  = now;

        processInput(win, dt);

        doRender();

        glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);

        screenShader.use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_tex);
        screenShader.setInt("screenTexture", 0);
        glBindVertexArray(g_quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        if (g_showGui) renderGui();

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    // ── Cleanup ──
    g_optix.cleanup();
    g_embree.cleanup();
    glDeleteVertexArrays(1, &g_quadVAO);
    glDeleteBuffers(1, &g_quadVBO);
    glDeleteTextures(1, &g_tex);
    if (g_pbo) glDeleteBuffers(1, &g_pbo);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return 0;
}