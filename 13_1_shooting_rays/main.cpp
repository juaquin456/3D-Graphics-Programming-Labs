#include <iostream>
#include <vector>
#include <cmath>
#include <limits>
#include <chrono>
#include <algorithm>
#include <string>
#include <sstream>
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
#include "common/Scene.h"
#include "common/Shader.h"
#include "common/Material.h"
#include "CudaRayTracer.h"

// -------------------------------------------------------------
// Global Application State & Settings
// -------------------------------------------------------------
int SCR_WIDTH = 1280;
int SCR_HEIGHT = 720;

Camera cam;
Scene scene;

// Scene geometric & material data uploaded to GPU
std::vector<CudaTriangle> g_worldTriangles;
std::vector<CudaMaterial> g_sceneMaterials;
glm::vec3 g_sceneMin{0.0f};
glm::vec3 g_sceneMax{0.0f};

// Window interaction state
bool isDragging = false;
double lastMouseX = 0, lastMouseY = 0;
bool showGui = true;

// CUDA Ray Tracer instance
CudaRayTracer g_cudaRayTracer;
bool g_cudaAvailable = false;
std::string g_cudaDeviceName = "";

// Light settings
CudaLight g_light;
bool g_animateLight = false;

// Ray tracer render settings
struct RayTraceSettings {
    int resolutionIndex = 1; // 0: 160x90, 1: 320x180, 2: 480x270, 3: 640x360, 4: 1280x720
    int renderWidth = 320;
    int renderHeight = 180;

    bool continuous = true;
    bool dirty = true;
    bool enableShadows = true;
    bool useSmoothNormals = true;
    int overrideMaterialIndex = -1; // -1: use scene material from OBJ; >= 0: preset override

    // Diagnostic readouts
    double lastKernelTimeMs = 0.0;
    int totalRays = 0;
} g_settings;

// Preset material names and definitions
const char* const MATERIAL_PRESET_NAMES[] = {
    "Scene Materials (from .obj / .mtl)",
    "Gold",
    "White Plastic",
    "Emerald",
    "Ruby",
    "Turquoise",
    "Chrome / Silver"
};
int g_selectedMaterialPreset = 0; // 0 = Scene material
int g_presetMaterialBaseIndex = 0; // Index in g_sceneMaterials where presets start

// Available scenes for quick switching
const char* const SCENE_NAMES[] = {
    "Utah Teapot (models/teapot.obj)",
    "Stanford Bunny (models/stanford-bunny.obj)",
    "Fireplace Room (scenes/fireplace_room/fireplace_room.obj)",
    "LPS Head (scenes/lpshead/head.OBJ)",
    "Custom Path"
};
const char* const SCENE_PATHS[] = {
    "../../models/teapot.obj",
    "../../models/stanford-bunny.obj",
    "../../scenes/fireplace_room/fireplace_room.obj",
    "../../scenes/lpshead/head.OBJ",
    ""
};
int g_selectedSceneIndex = 0;
char g_customScenePath[512] = "../../models/teapot.obj";

// OpenGL resources for displaying rendered ray-traced image
GLuint g_depthTexture = 0;
GLuint g_quadVAO = 0;
GLuint g_quadVBO = 0;
GLuint g_pbo = 0; // OpenGL Pixel Buffer Object for Zero-Copy CUDA Interop

// -------------------------------------------------------------
// Pre-extract world-space triangles, vertex normals, & materials
// -------------------------------------------------------------
void rebuildWorldScene() {
    g_worldTriangles.clear();
    g_sceneMaterials.clear();

    glm::mat4 sceneModel = scene.getModelMatrix();

    // Default fallback material (White Plastic)
    CudaMaterial defaultMat;
    defaultMat.Ka = glm::vec3(0.05f, 0.05f, 0.05f);
    defaultMat.Kd = glm::vec3(0.85f, 0.85f, 0.85f);
    defaultMat.Ks = glm::vec3(0.70f, 0.70f, 0.70f);
    defaultMat.shininess = 32.0f;

    for (const auto& obj : scene.objects) {
        if (!obj.meshAsset) continue;

        // 1. Extract and register material for this sub-mesh
        int matId = static_cast<int>(g_sceneMaterials.size());
        CudaMaterial objMat = defaultMat;

        if (auto phong = dynamic_cast<PhongMaterial*>(obj.material.get())) {
            objMat.Ka = phong->Ka;
            objMat.Kd = phong->Kd;
            objMat.Ks = phong->Ks;
            objMat.shininess = (phong->shininess > 0.0f) ? phong->shininess : 32.0f;
        } else if (auto texMat = dynamic_cast<TexturedMaterial*>(obj.material.get())) {
            objMat.Ka = glm::vec3(0.2f);
            objMat.Kd = glm::vec3(0.8f, 0.75f, 0.7f);
            objMat.Ks = glm::vec3(0.4f);
            objMat.shininess = (texMat->shininess > 0.0f) ? texMat->shininess : 32.0f;
        }
        g_sceneMaterials.push_back(objMat);

        // 2. Transform vertices and normals to world space
        glm::mat4 model = sceneModel * obj.getModelMatrix();
        glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(model)));
        const auto& mesh = obj.meshAsset->mesh;
        bool hasNormals = !mesh.normals.empty();

        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            int idx0 = mesh.indices[i];
            int idx1 = mesh.indices[i + 1];
            int idx2 = mesh.indices[i + 2];

            CudaTriangle tri;
            tri.v0 = glm::vec3(model * glm::vec4(mesh.vertices[idx0], 1.0f));
            tri.v1 = glm::vec3(model * glm::vec4(mesh.vertices[idx1], 1.0f));
            tri.v2 = glm::vec3(model * glm::vec4(mesh.vertices[idx2], 1.0f));

            if (hasNormals && idx0 < mesh.normals.size() && idx1 < mesh.normals.size() && idx2 < mesh.normals.size()) {
                tri.n0 = glm::normalize(normalMatrix * mesh.normals[idx0]);
                tri.n1 = glm::normalize(normalMatrix * mesh.normals[idx1]);
                tri.n2 = glm::normalize(normalMatrix * mesh.normals[idx2]);
            } else {
                glm::vec3 fn = glm::normalize(glm::cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
                tri.n0 = fn;
                tri.n1 = fn;
                tri.n2 = fn;
            }

            tri.materialId = matId;
            g_worldTriangles.push_back(tri);
        }
    }

    // Append Preset Materials to array for dynamic override in GUI
    g_presetMaterialBaseIndex = static_cast<int>(g_sceneMaterials.size());

    // 1: Gold
    g_sceneMaterials.push_back({ glm::vec3(0.247f, 0.199f, 0.074f), glm::vec3(0.751f, 0.606f, 0.226f), glm::vec3(0.628f, 0.556f, 0.366f), 51.2f });
    // 2: White Plastic
    g_sceneMaterials.push_back({ glm::vec3(0.05f, 0.05f, 0.05f), glm::vec3(0.85f, 0.85f, 0.85f), glm::vec3(0.70f, 0.70f, 0.70f), 32.0f });
    // 3: Emerald
    g_sceneMaterials.push_back({ glm::vec3(0.021f, 0.174f, 0.021f), glm::vec3(0.075f, 0.604f, 0.075f), glm::vec3(0.633f, 0.727f, 0.633f), 76.8f });
    // 4: Ruby
    g_sceneMaterials.push_back({ glm::vec3(0.174f, 0.011f, 0.011f), glm::vec3(0.614f, 0.041f, 0.041f), glm::vec3(0.727f, 0.626f, 0.626f), 76.8f });
    // 5: Turquoise
    g_sceneMaterials.push_back({ glm::vec3(0.100f, 0.187f, 0.174f), glm::vec3(0.396f, 0.741f, 0.691f), glm::vec3(0.297f, 0.308f, 0.306f), 12.8f });
    // 6: Chrome / Silver
    g_sceneMaterials.push_back({ glm::vec3(0.250f, 0.250f, 0.250f), glm::vec3(0.400f, 0.400f, 0.400f), glm::vec3(0.770f, 0.770f, 0.770f), 89.6f });

    // Compute scene bounds
    if (g_worldTriangles.empty()) {
        g_sceneMin = glm::vec3(-1.0f);
        g_sceneMax = glm::vec3(1.0f);
    } else {
        g_sceneMin = glm::vec3(std::numeric_limits<float>::infinity());
        g_sceneMax = glm::vec3(-std::numeric_limits<float>::infinity());
        for (const auto& tri : g_worldTriangles) {
            g_sceneMin = glm::min(g_sceneMin, glm::min(tri.v0, glm::min(tri.v1, tri.v2)));
            g_sceneMax = glm::max(g_sceneMax, glm::max(tri.v0, glm::max(tri.v1, tri.v2)));
        }
    }

    std::cout << "[Scene] Extracted " << g_worldTriangles.size() << " world-space triangles." << std::endl;
    std::cout << "[Scene] Extracted " << g_sceneMaterials.size() << " materials (including presets)." << std::endl;

    // Upload to GPU
    if (g_cudaAvailable) {
        g_cudaRayTracer.uploadScene(g_worldTriangles, g_sceneMaterials);
        std::cout << "[CUDA] Uploaded " << g_worldTriangles.size() << " triangles & " 
                  << g_sceneMaterials.size() << " materials to GPU." << std::endl;
    }
}

// -------------------------------------------------------------
// Frame camera to look at the scene bounding box
// -------------------------------------------------------------
void frameCamera() {
    glm::vec3 center = (g_sceneMin + g_sceneMax) * 0.5f;
    float radius = glm::length(g_sceneMax - g_sceneMin) * 0.5f;
    if (radius < 1e-4f) radius = 1.0f;

    cam.target = center;
    float fovRad = glm::radians(cam.fov_deg);
    float dist = (radius / std::tan(fovRad * 0.5f)) * 1.25f;
    cam.position = center + glm::vec3(0.0f, radius * 0.25f, dist);
    cam.near_z = std::max(0.01f, dist * 0.01f);
    cam.far_z = dist * 15.0f;
    cam.speed = radius * 0.8f;

    // Reposition light relative to scene
    g_light.position = center + glm::vec3(radius * 0.8f, radius * 1.5f, radius * 1.0f);

    g_settings.dirty = true;
}

// -------------------------------------------------------------
// Setup OpenGL Pixel Buffer Object (PBO) for Zero-Copy CUDA Interop
// -------------------------------------------------------------
void setupPBO(int width, int height) {
    if (g_cudaAvailable) {
        g_cudaRayTracer.unregisterPBO();
    }

    if (g_pbo == 0) {
        glGenBuffers(1, &g_pbo);
    }

    // Allocate GPU buffer memory for PBO (RGBA: 4 bytes per pixel)
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, g_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, width * height * 4, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    // Reallocate texture storage
    glBindTexture(GL_TEXTURE_2D, g_depthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (g_cudaAvailable) {
        g_cudaRayTracer.registerPBO(g_pbo);
    }
}

// -------------------------------------------------------------
// Update resolution according to index
// -------------------------------------------------------------
void applyResolutionIndex(int idx) {
    g_settings.resolutionIndex = idx;
    switch (idx) {
        case 0: g_settings.renderWidth = 160; g_settings.renderHeight = 90;  break;
        case 1: g_settings.renderWidth = 320; g_settings.renderHeight = 180; break;
        case 2: g_settings.renderWidth = 480; g_settings.renderHeight = 270; break;
        case 3: g_settings.renderWidth = 640; g_settings.renderHeight = 360; break;
        case 4: g_settings.renderWidth = 1280; g_settings.renderHeight = 720; break;
        default: g_settings.renderWidth = 320; g_settings.renderHeight = 180; break;
    }
    setupPBO(g_settings.renderWidth, g_settings.renderHeight);
    g_settings.dirty = true;
}

// -------------------------------------------------------------
// Load a scene from file path
// -------------------------------------------------------------
void loadScene(const std::string& filepath) {
    std::cout << "[App] Loading scene: " << filepath << std::endl;
    Scene newScene = Scene::loadOBJ(filepath);
    if (!newScene.objects.empty()) {
        scene = std::move(newScene);
        rebuildWorldScene();
        frameCamera();
    } else {
        std::cerr << "[App] Warning: Scene loaded with 0 objects from " << filepath << std::endl;
    }
}

// -------------------------------------------------------------
// CUDA Ray Tracer Execution (Direct VRAM Write via PBO)
// -------------------------------------------------------------
void renderRayTrace() {
    int width = g_settings.renderWidth;
    int height = g_settings.renderHeight;
    g_settings.totalRays = width * height;

    if (!g_cudaAvailable) return;

    glm::mat4 viewMatrix = cam.getViewMatrix();
    glm::mat4 projMatrix = cam.getProjectionMatrix();
    glm::mat4 invProj = glm::inverse(projMatrix);
    glm::mat4 invView = glm::inverse(viewMatrix);

    double cudaMs = 0.0;

    int overrideIdx = -1;
    if (g_selectedMaterialPreset > 0) {
        overrideIdx = g_presetMaterialBaseIndex + (g_selectedMaterialPreset - 1);
    }

    // Zero-Copy CUDA Ray Tracing directly into the OpenGL PBO
    bool success = g_cudaRayTracer.renderRayTraceToPBO(
        invProj,
        invView,
        cam.position,
        g_light,
        width,
        height,
        g_settings.enableShadows,
        g_settings.useSmoothNormals,
        overrideIdx,
        cudaMs
    );

    if (success) {
        g_settings.lastKernelTimeMs = cudaMs;

        // Direct GPU VRAM to Texture DMA transfer (0 CPU copies)
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, g_pbo);
        glBindTexture(GL_TEXTURE_2D, g_depthTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, (void*)0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    } else {
        std::cerr << "[CUDA] Warning: Ray trace kernel execution failed." << std::endl;
    }

    g_settings.dirty = false;
}

// -------------------------------------------------------------
// GLFW Window Callbacks
// -------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    SCR_WIDTH = width;
    SCR_HEIGHT = height;
    cam.aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
    g_settings.dirty = true;
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    glm::vec3 viewDir = cam.position - cam.target;
    float currentDist = glm::length(viewDir);

    float zoomSensitivity = 0.15f * cam.speed;
    float newDist = currentDist - static_cast<float>(yoffset) * zoomSensitivity;
    if (newDist < 0.05f) newDist = 0.05f;

    if (currentDist > 1e-5f) {
        cam.position = cam.target + glm::normalize(viewDir) * newDist;
        g_settings.dirty = true;
    }
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            isDragging = true;
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        } else if (action == GLFW_RELEASE) {
            isDragging = false;
        }
    }
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    if (isDragging) {
        float deltaX = static_cast<float>(xpos - lastMouseX);
        float deltaY = static_cast<float>(ypos - lastMouseY);

        lastMouseX = xpos;
        lastMouseY = ypos;

        cam.rotateOrbit(deltaX, deltaY, 0.005f);
        g_settings.dirty = true;
    }
}

void process_input(GLFWwindow* window, float deltaTime) {
    ImGuiIO& io = ImGui::GetIO();

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (io.WantCaptureKeyboard) return;

    bool moved = false;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { cam.moveForward(deltaTime); moved = true; }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { cam.moveBackward(deltaTime); moved = true; }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { cam.moveLeft(deltaTime); moved = true; }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { cam.moveRight(deltaTime); moved = true; }

    if (moved) {
        g_settings.dirty = true;
    }
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;

    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_SPACE) {
            g_settings.continuous = !g_settings.continuous;
            g_settings.dirty = true;
        } else if (key == GLFW_KEY_R) {
            g_settings.dirty = true;
        } else if (key == GLFW_KEY_S) {
            g_settings.enableShadows = !g_settings.enableShadows;
            g_settings.dirty = true;
        } else if (key == GLFW_KEY_N) {
            g_settings.useSmoothNormals = !g_settings.useSmoothNormals;
            g_settings.dirty = true;
        } else if (key == GLFW_KEY_F) {
            frameCamera();
        } else if (key == GLFW_KEY_H) {
            showGui = !showGui;
        } else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_5) {
            applyResolutionIndex(key - GLFW_KEY_1);
        }
    }
}

// -------------------------------------------------------------
// Setup Fullscreen Quad for Texture Visualization
// -------------------------------------------------------------
void setupScreenQuad() {
    float quadVertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &g_quadVAO);
    glGenBuffers(1, &g_quadVBO);

    glBindVertexArray(g_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);

    glGenTextures(1, &g_depthTexture);
    glBindTexture(GL_TEXTURE_2D, g_depthTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// -------------------------------------------------------------
// Render Dear ImGui Control Panel
// -------------------------------------------------------------
void renderGui() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430, 680), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Task 13.1: CUDA Ray Tracer (Materials & Shadows)", &showGui)) {
        ImGuiIO& io = ImGui::GetIO();

        // 1. Ray Tracing Performance & GPU Engine
        if (ImGui::CollapsingHeader("GPU Ray Tracer Status", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("OpenGL Window: %.1f FPS (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "CUDA Kernel: %.2f ms", g_settings.lastKernelTimeMs);
            ImGui::Text("GPU Device: %s", g_cudaDeviceName.c_str());

            double mrays = (g_settings.lastKernelTimeMs > 0.0) 
                ? (g_settings.totalRays / (g_settings.lastKernelTimeMs * 1000.0)) 
                : 0.0;
            ImGui::Text("Throughput: %.2f MRays/sec", mrays);
            ImGui::Text("Resolution: %d x %d (%d rays)", g_settings.renderWidth, g_settings.renderHeight, g_settings.totalRays);
            ImGui::Text("Triangles in Scene: %zu", g_worldTriangles.size());
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "Zero-Copy PBO: Active (0 CPU copies)");
            ImGui::Separator();
        }

        // 2. Material System
        if (ImGui::CollapsingHeader("Material System", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Combo("Material Selection", &g_selectedMaterialPreset, MATERIAL_PRESET_NAMES, IM_ARRAYSIZE(MATERIAL_PRESET_NAMES))) {
                g_settings.dirty = true;
            }

            if (g_selectedMaterialPreset == 0) {
                ImGui::TextDisabled("Using materials parsed from OBJ / MTL file (%d defined)", g_presetMaterialBaseIndex);
            } else {
                int activeMatIdx = g_presetMaterialBaseIndex + (g_selectedMaterialPreset - 1);
                if (activeMatIdx < g_sceneMaterials.size()) {
                    CudaMaterial& m = g_sceneMaterials[activeMatIdx];
                    if (ImGui::ColorEdit3("Ambient (Ka)", glm::value_ptr(m.Ka))) g_settings.dirty = true;
                    if (ImGui::ColorEdit3("Diffuse (Kd)", glm::value_ptr(m.Kd))) g_settings.dirty = true;
                    if (ImGui::ColorEdit3("Specular (Ks)", glm::value_ptr(m.Ks))) g_settings.dirty = true;
                    if (ImGui::SliderFloat("Shininess", &m.shininess, 1.0f, 128.0f)) g_settings.dirty = true;
                }
            }
        }

        // 3. Shading & Lighting Controls
        if (ImGui::CollapsingHeader("Lighting & Shading Options", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable Hard Shadows [Shadow Rays] (S)", &g_settings.enableShadows)) {
                g_settings.dirty = true;
            }

            if (ImGui::Checkbox("Smooth Vertex Normals (N)", &g_settings.useSmoothNormals)) {
                g_settings.dirty = true;
            }

            if (ImGui::Checkbox("Animate Light Orbit", &g_animateLight)) {
                g_settings.dirty = true;
            }

            if (ImGui::DragFloat3("Light Position", glm::value_ptr(g_light.position), 0.1f)) {
                g_settings.dirty = true;
            }

            if (ImGui::ColorEdit3("Light Ambient", glm::value_ptr(g_light.ambient))) g_settings.dirty = true;
            if (ImGui::ColorEdit3("Light Diffuse", glm::value_ptr(g_light.diffuse))) g_settings.dirty = true;
            if (ImGui::ColorEdit3("Light Specular", glm::value_ptr(g_light.specular))) g_settings.dirty = true;
        }

        // 4. Resolution & Execution Controls
        if (ImGui::CollapsingHeader("Resolution & Ray Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Render Primary Rays Now (R)", ImVec2(-1, 30))) {
                g_settings.dirty = true;
            }

            if (ImGui::Checkbox("Continuous Real-Time Ray Cast (Space)", &g_settings.continuous)) {
                g_settings.dirty = true;
            }

            const char* resOptions[] = {
                "160 x 90  (Ultra Fast)",
                "320 x 180 (Fast - Real-time)",
                "480 x 270 (Balanced)",
                "640 x 360 (High Quality)",
                "1280 x 720 (Full HD)"
            };
            if (ImGui::Combo("Resolution", &g_settings.resolutionIndex, resOptions, IM_ARRAYSIZE(resOptions))) {
                applyResolutionIndex(g_settings.resolutionIndex);
            }
        }

        // 5. Scene Selection & Camera
        if (ImGui::CollapsingHeader("Scene & Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Combo("Active Scene", &g_selectedSceneIndex, SCENE_NAMES, IM_ARRAYSIZE(SCENE_NAMES))) {
                if (g_selectedSceneIndex < 4) {
                    loadScene(SCENE_PATHS[g_selectedSceneIndex]);
                }
            }

            if (g_selectedSceneIndex == 4) {
                ImGui::InputText("OBJ Path", g_customScenePath, sizeof(g_customScenePath));
                if (ImGui::Button("Load Custom OBJ")) {
                    loadScene(g_customScenePath);
                }
            }

            if (ImGui::Button("Frame Camera to Scene (F)", ImVec2(-1, 24))) {
                frameCamera();
            }

            ImGui::Separator();
            ImGui::Text("Camera Eye:    (%.2f, %.2f, %.2f)", cam.position.x, cam.position.y, cam.position.z);
            ImGui::Text("Camera Target: (%.2f, %.2f, %.2f)", cam.target.x, cam.target.y, cam.target.z);
            if (ImGui::SliderFloat("Camera FOV", &cam.fov_deg, 15.0f, 90.0f)) {
                g_settings.dirty = true;
            }
        }

        // 6. Help & Keybindings
        if (ImGui::CollapsingHeader("Keyboard Controls")) {
            ImGui::BulletText("Left Mouse Drag: Orbit Camera");
            ImGui::BulletText("Mouse Scroll: Zoom Camera");
            ImGui::BulletText("W, A, S, D: Move Camera Position");
            ImGui::BulletText("Space: Toggle Real-time Ray Shooting");
            ImGui::BulletText("S: Toggle Shadow Rays");
            ImGui::BulletText("N: Toggle Smooth vs Flat Normals");
            ImGui::BulletText("R: Trigger Ray Casting");
            ImGui::BulletText("F: Reset / Frame Camera");
            ImGui::BulletText("1-5: Change Ray Cast Resolution");
            ImGui::BulletText("H: Toggle this GUI overlay");
        }
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// -------------------------------------------------------------
// Main Entry Point
// -------------------------------------------------------------
int main(int argc, char** argv) {
    if (!glfwInit()) {
        std::cerr << "[GLFW] Error: Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Task 13.1: CUDA Ray Tracer - Materials & Shading", NULL, NULL);
    if (!window) {
        std::cerr << "[GLFW] Error: Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // Uncapped framerate

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "[GLAD] Error: Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetKeyCallback(window, key_callback);

    // Initialize Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Setup fullscreen quad and display shader
    setupScreenQuad();
    Shader screenShader("../../shaders/screen_quad.vert", "../../shaders/screen_quad.frag");

    // Check CUDA availability
    g_cudaAvailable = CudaRayTracer::isAvailable();
    if (g_cudaAvailable) {
        g_cudaDeviceName = CudaRayTracer::getDeviceName();
        std::cout << "[CUDA] Initialized GPU: " << g_cudaDeviceName << std::endl;
    } else {
        std::cerr << "[CUDA] Error: No CUDA-capable GPU found! Exiting." << std::endl;
        return -1;
    }

    // Setup Pixel Buffer Object (PBO)
    setupPBO(g_settings.renderWidth, g_settings.renderHeight);

    // Load initial scene
    std::string initialScene = "../../models/teapot.obj";
    if (argc > 1) {
        initialScene = argv[1];
        strncpy(g_customScenePath, argv[1], sizeof(g_customScenePath) - 1);
        g_selectedSceneIndex = 4;
    }
    loadScene(initialScene);

    // Initial ray cast
    renderRayTrace();

    float lastFrameTime = static_cast<float>(glfwGetTime());

    // Main Render Loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrameTime = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        process_input(window, deltaTime);

        if (g_animateLight) {
            float t = currentFrameTime * 1.5f;
            float radius = glm::length(g_sceneMax - g_sceneMin) * 0.7f;
            glm::vec3 center = (g_sceneMin + g_sceneMax) * 0.5f;
            g_light.position = center + glm::vec3(std::cos(t) * radius, radius * 1.2f, std::sin(t) * radius);
            g_settings.dirty = true;
        }

        // Perform ray shooting if real-time continuous mode is active or settings changed
        if (g_settings.continuous && g_settings.dirty) {
            renderRayTrace();
        } else if (!g_settings.continuous && g_settings.dirty) {
            renderRayTrace();
        }

        // Render OpenGL Screen Quad with Texture
        glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDisable(GL_DEPTH_TEST);
        screenShader.use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_depthTexture);
        screenShader.setInt("screenTexture", 0);

        glBindVertexArray(g_quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        // Render ImGui GUI
        if (showGui) {
            renderGui();
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup resources
    glDeleteVertexArrays(1, &g_quadVAO);
    glDeleteBuffers(1, &g_quadVBO);
    glDeleteTextures(1, &g_depthTexture);
    if (g_pbo != 0) {
        glDeleteBuffers(1, &g_pbo);
    }

    g_cudaRayTracer.cleanup();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwTerminate();
    return 0;
}
