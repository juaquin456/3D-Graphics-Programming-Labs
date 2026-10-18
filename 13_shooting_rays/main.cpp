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

#if HAVE_CUDA
#include "CudaRayTracer.h"
#endif

// -------------------------------------------------------------
// Structure to represent a triangle in world space
// -------------------------------------------------------------
struct Triangle {
    glm::vec3 v0;
    glm::vec3 v1;
    glm::vec3 v2;
};

// -------------------------------------------------------------
// Compute Backend: CPU vs CUDA GPU
// -------------------------------------------------------------
enum class ComputeBackend {
    CPU = 0,
    CUDA = 1
};

ComputeBackend g_backend = ComputeBackend::CPU;

#if HAVE_CUDA
CudaRayTracer g_cudaRayTracer;
bool g_cudaAvailable = false;
std::string g_cudaDeviceName = "";
#endif

inline bool rayTriangleIntersect(const Ray& ray, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& outT) {
    const float EPSILON = 1e-7f;
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 h = glm::cross(ray.direction, edge2);
    float a = glm::dot(edge1, h);

    // Double-sided test: do not cull backfaces
    if (a > -EPSILON && a < EPSILON) return false;

    float f = 1.0f / a;
    glm::vec3 s = ray.origin - v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(ray.direction, q);
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = f * glm::dot(edge2, q);
    if (t > EPSILON) {
        outT = t;
        return true;
    }
    return false;
}

// -------------------------------------------------------------
// Global Application State & Settings
// -------------------------------------------------------------
int SCR_WIDTH = 1280;
int SCR_HEIGHT = 720;

Camera cam;
Scene scene;
std::vector<Triangle> g_worldTriangles;
glm::vec3 g_sceneMin{0.0f};
glm::vec3 g_sceneMax{0.0f};

// Window interaction state
bool isDragging = false;
double lastMouseX = 0, lastMouseY = 0;
bool showGui = true;

// Depth map render settings
struct DepthSettings {
    int resolutionIndex = 1; // 0: 160x90, 1: 320x180, 2: 480x270, 3: 640x360, 4: 1280x720
    int renderWidth = 320;
    int renderHeight = 180;

    bool continuous = true;
    bool dirty = true;
    bool autoRange = true;
    float manualMin = 1.0f;
    float manualMax = 10.0f;
    bool invertDepth = false;      // false: near=0 (black), far=1 (white); true: near=1 (white), far=0 (black)
    bool usePlanarDepth = false;   // false: Euclidean distance t; true: Z-depth along camera front
    bool linearFiltering = false;

    // Diagnostic readouts
    float detectedMinDepth = 0.0f;
    float detectedMaxDepth = 1.0f;
    double lastRenderTimeMs = 0.0;
    int totalRays = 0;
    int hitCount = 0;

    // Benchmark comparison
    double lastCpuTimeMs = 0.0;
    double lastCudaTimeMs = 0.0;
} g_depthSettings;

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

// OpenGL resources for displaying depth texture
GLuint g_depthTexture = 0;
GLuint g_quadVAO = 0;
GLuint g_quadVBO = 0;
GLuint g_pbo = 0; // OpenGL Pixel Buffer Object for Zero-Copy CUDA Interop

// CPU buffers
std::vector<float> g_depthBuffer;
std::vector<unsigned char> g_pixelBuffer;

// -------------------------------------------------------------
// Pre-extract world-space triangles from loaded scene
// -------------------------------------------------------------
void rebuildWorldTriangles() {
    g_worldTriangles.clear();
    glm::mat4 sceneModel = scene.getModelMatrix();

    for (const auto& obj : scene.objects) {
        if (!obj.meshAsset) continue;
        glm::mat4 model = sceneModel * obj.getModelMatrix();
        const auto& mesh = obj.meshAsset->mesh;

        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            glm::vec3 v0 = glm::vec3(model * glm::vec4(mesh.vertices[mesh.indices[i]], 1.0f));
            glm::vec3 v1 = glm::vec3(model * glm::vec4(mesh.vertices[mesh.indices[i + 1]], 1.0f));
            glm::vec3 v2 = glm::vec3(model * glm::vec4(mesh.vertices[mesh.indices[i + 2]], 1.0f));
            g_worldTriangles.push_back({v0, v1, v2});
        }
    }

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
    std::cout << "[Scene] Bounds Min: (" << g_sceneMin.x << ", " << g_sceneMin.y << ", " << g_sceneMin.z << ")" << std::endl;
    std::cout << "[Scene] Bounds Max: (" << g_sceneMax.x << ", " << g_sceneMax.y << ", " << g_sceneMax.z << ")" << std::endl;

#if HAVE_CUDA
    if (g_cudaAvailable) {
        std::vector<CudaTriangle> cudaTris;
        cudaTris.reserve(g_worldTriangles.size());
        for (const auto& tri : g_worldTriangles) {
            cudaTris.push_back({tri.v0, tri.v1, tri.v2});
        }
        g_cudaRayTracer.uploadTriangles(cudaTris);
        std::cout << "[CUDA] Uploaded " << cudaTris.size() << " triangles to GPU." << std::endl;
    }
#endif
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
    g_depthSettings.dirty = true;
}

// -------------------------------------------------------------
// Setup OpenGL Pixel Buffer Object (PBO) for Zero-Copy CUDA Interop
// -------------------------------------------------------------
void setupPBO(int width, int height) {
#if HAVE_CUDA
    if (g_cudaAvailable) {
        g_cudaRayTracer.unregisterPBO();
    }
#endif

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

#if HAVE_CUDA
    if (g_cudaAvailable) {
        g_cudaRayTracer.registerPBO(g_pbo);
    }
#endif
}

// -------------------------------------------------------------
// Update resolution according to index
// -------------------------------------------------------------
void applyResolutionIndex(int idx) {
    g_depthSettings.resolutionIndex = idx;
    switch (idx) {
        case 0: g_depthSettings.renderWidth = 160; g_depthSettings.renderHeight = 90;  break;
        case 1: g_depthSettings.renderWidth = 320; g_depthSettings.renderHeight = 180; break;
        case 2: g_depthSettings.renderWidth = 480; g_depthSettings.renderHeight = 270; break;
        case 3: g_depthSettings.renderWidth = 640; g_depthSettings.renderHeight = 360; break;
        case 4: g_depthSettings.renderWidth = 1280; g_depthSettings.renderHeight = 720; break;
        default: g_depthSettings.renderWidth = 320; g_depthSettings.renderHeight = 180; break;
    }
    setupPBO(g_depthSettings.renderWidth, g_depthSettings.renderHeight);
    g_depthSettings.dirty = true;
}

// -------------------------------------------------------------
// Load a scene from file path
// -------------------------------------------------------------
void loadScene(const std::string& filepath) {
    std::cout << "[App] Loading scene: " << filepath << std::endl;
    Scene newScene = Scene::loadOBJ(filepath);
    if (!newScene.objects.empty()) {
        scene = std::move(newScene);
        rebuildWorldTriangles();
        frameCamera();
    } else {
        std::cerr << "[App] Warning: Scene loaded with 0 objects from " << filepath << std::endl;
    }
}

// -------------------------------------------------------------
// CPU Ray Shooting Kernel
// -------------------------------------------------------------
void executeCpuRayCast(int width, int height) {
    auto startTime = std::chrono::high_resolution_clock::now();

    glm::mat4 viewMatrix = cam.getViewMatrix();
    glm::mat4 projMatrix = cam.getProjectionMatrix();
    glm::mat4 invProj = glm::inverse(projMatrix);
    glm::mat4 invView = glm::inverse(viewMatrix);
    glm::vec3 camPos = cam.position;
    glm::vec3 camFront = cam.getFront();

    const size_t numTriangles = g_worldTriangles.size();
    const Triangle* triData = g_worldTriangles.data();

    #pragma omp parallel for schedule(dynamic, 8)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float ndcX = (2.0f * (static_cast<float>(x) + 0.5f)) / static_cast<float>(width) - 1.0f;
            float ndcY = (2.0f * (static_cast<float>(y) + 0.5f)) / static_cast<float>(height) - 1.0f;

            glm::vec4 clipCoords(ndcX, ndcY, -1.0f, 1.0f);
            glm::vec4 eyeCoords = invProj * clipCoords;
            eyeCoords = glm::vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f);

            glm::vec3 rayDir = glm::normalize(glm::vec3(invView * eyeCoords));
            Ray ray(camPos, rayDir);

            float closestT = std::numeric_limits<float>::infinity();
            for (size_t t = 0; t < numTriangles; ++t) {
                float dist;
                if (rayTriangleIntersect(ray, triData[t].v0, triData[t].v1, triData[t].v2, dist)) {
                    if (dist < closestT) {
                        closestT = dist;
                    }
                }
            }

            if (closestT < std::numeric_limits<float>::infinity()) {
                if (g_depthSettings.usePlanarDepth) {
                    g_depthBuffer[y * width + x] = closestT * glm::dot(rayDir, camFront);
                } else {
                    g_depthBuffer[y * width + x] = closestT;
                }
            }
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double cpuMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    g_depthSettings.lastRenderTimeMs = cpuMs;
    g_depthSettings.lastCpuTimeMs = cpuMs;

    // Compute minimum and maximum hit depths
    int numPixels = width * height;
    float minHit = std::numeric_limits<float>::infinity();
    float maxHit = -std::numeric_limits<float>::infinity();
    int hits = 0;

    for (int i = 0; i < numPixels; ++i) {
        float d = g_depthBuffer[i];
        if (d < std::numeric_limits<float>::infinity()) {
            if (d < minHit) minHit = d;
            if (d > maxHit) maxHit = d;
            hits++;
        }
    }

    if (hits == 0) {
        minHit = 0.0f;
        maxHit = 1.0f;
    }
    g_depthSettings.detectedMinDepth = minHit;
    g_depthSettings.detectedMaxDepth = maxHit;
    g_depthSettings.hitCount = hits;

    // Map depth values to pixel intensities in [0:1] (RGBA)
    float effMin = g_depthSettings.autoRange ? minHit : g_depthSettings.manualMin;
    float effMax = g_depthSettings.autoRange ? maxHit : g_depthSettings.manualMax;
    if (effMax - effMin < 1e-6f) {
        effMax = effMin + 1.0f;
    }

    g_pixelBuffer.resize(numPixels * 4);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < numPixels; ++i) {
        float d = g_depthBuffer[i];
        float intensity = 0.0f;

        if (d >= std::numeric_limits<float>::infinity()) {
            intensity = g_depthSettings.invertDepth ? 0.0f : 1.0f;
        } else {
            float normalized = (d - effMin) / (effMax - effMin);
            normalized = glm::clamp(normalized, 0.0f, 1.0f);

            if (g_depthSettings.invertDepth) {
                intensity = 1.0f - normalized; // Near = White, Far = Black
            } else {
                intensity = normalized;        // Near = Black, Far = White (Standard OpenGL)
            }
        }

        uint8_t byteVal = static_cast<uint8_t>(glm::clamp(intensity * 255.0f, 0.0f, 255.0f));
        g_pixelBuffer[i * 4 + 0] = byteVal;
        g_pixelBuffer[i * 4 + 1] = byteVal;
        g_pixelBuffer[i * 4 + 2] = byteVal;
        g_pixelBuffer[i * 4 + 3] = 255;
    }

    // Upload CPU pixel buffer directly to OpenGL Texture
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); // Ensure PBO is unbound
    glBindTexture(GL_TEXTURE_2D, g_depthTexture);
    GLint filter = g_depthSettings.linearFiltering ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, g_pixelBuffer.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

// -------------------------------------------------------------
// Unified Depth Map Renderer
// -------------------------------------------------------------
void renderDepthMap() {
    int width = g_depthSettings.renderWidth;
    int height = g_depthSettings.renderHeight;
    int numPixels = width * height;
    g_depthSettings.totalRays = numPixels;

    bool usedCudaZeroCopy = false;

#if HAVE_CUDA
    if (g_backend == ComputeBackend::CUDA && g_cudaAvailable) {
        glm::mat4 viewMatrix = cam.getViewMatrix();
        glm::mat4 projMatrix = cam.getProjectionMatrix();
        glm::mat4 invProj = glm::inverse(projMatrix);
        glm::mat4 invView = glm::inverse(viewMatrix);

        double cudaMs = 0.0;
        float detectedMin = 0.0f, detectedMax = 1.0f;
        int hitCount = 0;

        bool success = g_cudaRayTracer.renderDepthToPBO(
            invProj,
            invView,
            cam.position,
            cam.getFront(),
            width,
            height,
            g_depthSettings.usePlanarDepth,
            g_depthSettings.autoRange,
            g_depthSettings.manualMin,
            g_depthSettings.manualMax,
            g_depthSettings.invertDepth,
            detectedMin,
            detectedMax,
            hitCount,
            cudaMs
        );

        if (success) {
            g_depthSettings.lastRenderTimeMs = cudaMs;
            g_depthSettings.lastCudaTimeMs = cudaMs;
            g_depthSettings.detectedMinDepth = detectedMin;
            g_depthSettings.detectedMaxDepth = detectedMax;
            g_depthSettings.hitCount = hitCount;

            // Direct GPU VRAM to Texture DMA transfer (0 CPU copies)
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, g_pbo);
            glBindTexture(GL_TEXTURE_2D, g_depthTexture);
            GLint filter = g_depthSettings.linearFiltering ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, (void*)0);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, 0);

            usedCudaZeroCopy = true;
        } else {
            std::cerr << "[CUDA] Warning: Render failed, falling back to CPU." << std::endl;
        }
    }
#endif

    if (!usedCudaZeroCopy) {
        g_depthBuffer.assign(numPixels, std::numeric_limits<float>::infinity());
        executeCpuRayCast(width, height);
    }

    g_depthSettings.dirty = false;
}

// -------------------------------------------------------------
// GLFW Window Callbacks
// -------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    SCR_WIDTH = width;
    SCR_HEIGHT = height;
    cam.aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
    g_depthSettings.dirty = true;
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
        g_depthSettings.dirty = true;
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
        g_depthSettings.dirty = true;
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
        g_depthSettings.dirty = true;
    }
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;

    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_SPACE) {
            g_depthSettings.continuous = !g_depthSettings.continuous;
            g_depthSettings.dirty = true;
        } else if (key == GLFW_KEY_R) {
            g_depthSettings.dirty = true;
        } else if (key == GLFW_KEY_B) {
#if HAVE_CUDA
            if (g_cudaAvailable) {
                g_backend = (g_backend == ComputeBackend::CPU) ? ComputeBackend::CUDA : ComputeBackend::CPU;
                g_depthSettings.dirty = true;
            }
#endif
        } else if (key == GLFW_KEY_I) {
            g_depthSettings.invertDepth = !g_depthSettings.invertDepth;
            g_depthSettings.dirty = true;
        } else if (key == GLFW_KEY_M) {
            g_depthSettings.usePlanarDepth = !g_depthSettings.usePlanarDepth;
            g_depthSettings.dirty = true;
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
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

    if (ImGui::Begin("Task 13: Shooting Rays (Depth Map)", &showGui)) {
        ImGuiIO& io = ImGui::GetIO();

        // 1. Compute Backend Selection (CPU vs CUDA Zero-Copy)
        if (ImGui::CollapsingHeader("Compute Backend", ImGuiTreeNodeFlags_DefaultOpen)) {
            int currentBackendIdx = (g_backend == ComputeBackend::CUDA) ? 1 : 0;

            if (ImGui::RadioButton("CPU (Multithreaded OpenMP)", currentBackendIdx == 0)) {
                g_backend = ComputeBackend::CPU;
                g_depthSettings.dirty = true;
            }

#if HAVE_CUDA
            if (g_cudaAvailable) {
                ImGui::SameLine();
                if (ImGui::RadioButton("CUDA (Zero-Copy PBO)", currentBackendIdx == 1)) {
                    g_backend = ComputeBackend::CUDA;
                    g_depthSettings.dirty = true;
                }
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Device: %s", g_cudaDeviceName.c_str());
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("CUDA (Not Available)");
            }
#else
            ImGui::SameLine();
            ImGui::TextDisabled("CUDA (Not Compiled)");
#endif
            if (g_backend == ComputeBackend::CUDA) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "[Interop] Zero-Copy OpenGL PBO: ACTIVE");
                ImGui::TextDisabled("-> 0 CPU copies. Direct CUDA GPU VRAM to Texture.");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "[CPU Mode] OpenMP 12 Cores -> glTexSubImage2D");
            }
            ImGui::Separator();
        }

        // 2. Performance and Ray Metrics
        if (ImGui::CollapsingHeader("Ray Casting Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("OpenGL Window: %.1f FPS (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);

            if (g_backend == ComputeBackend::CUDA) {
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "CUDA GPU Kernel Time: %.2f ms", g_depthSettings.lastRenderTimeMs);
            } else {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "CPU Ray Cast Time: %.2f ms", g_depthSettings.lastRenderTimeMs);
            }

            if (g_depthSettings.lastCpuTimeMs > 0.0 && g_depthSettings.lastCudaTimeMs > 0.0) {
                double speedup = g_depthSettings.lastCpuTimeMs / g_depthSettings.lastCudaTimeMs;
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "CUDA Speedup vs CPU: %.1fx faster", speedup);
            }

            double mrays = (g_depthSettings.lastRenderTimeMs > 0.0) 
                ? (g_depthSettings.totalRays / (g_depthSettings.lastRenderTimeMs * 1000.0)) 
                : 0.0;
            ImGui::Text("Throughput: %.2f MRays/sec", mrays);
            ImGui::Text("Rays Cast: %d (%dx%d)", g_depthSettings.totalRays, g_depthSettings.renderWidth, g_depthSettings.renderHeight);
            ImGui::Text("Rays Hit: %d / %d (%.1f%%)", g_depthSettings.hitCount, g_depthSettings.totalRays,
                        g_depthSettings.totalRays > 0 ? (100.0f * g_depthSettings.hitCount / g_depthSettings.totalRays) : 0.0f);
            ImGui::Text("Triangles in Scene: %zu", g_worldTriangles.size());
        }

        // 3. Render Trigger and Resolution
        if (ImGui::CollapsingHeader("Resolution & Execution", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Render Primary Rays Now (R)", ImVec2(-1, 30))) {
                g_depthSettings.dirty = true;
            }

            if (ImGui::Checkbox("Continuous Real-Time Ray Cast (Space)", &g_depthSettings.continuous)) {
                g_depthSettings.dirty = true;
            }

            const char* resOptions[] = {
                "160 x 90  (Ultra Fast)",
                "320 x 180 (Fast - Real-time)",
                "480 x 270 (Balanced)",
                "640 x 360 (High Quality)",
                "1280 x 720 (Full HD)"
            };
            if (ImGui::Combo("Resolution", &g_depthSettings.resolutionIndex, resOptions, IM_ARRAYSIZE(resOptions))) {
                applyResolutionIndex(g_depthSettings.resolutionIndex);
            }

            if (ImGui::Checkbox("Linear Texture Filtering", &g_depthSettings.linearFiltering)) {
                g_depthSettings.dirty = true;
            }
        }

        // 4. Depth Buffer Mapping to Grayscale [0:1]
        if (ImGui::CollapsingHeader("Depth Buffer Grayscale Mapping [0:1]", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* depthModes[] = { "Euclidean Distance (Ray t)", "Planar View Depth (Z-axis)" };
            int currentDepthMode = g_depthSettings.usePlanarDepth ? 1 : 0;
            if (ImGui::Combo("Depth Metric (M)", &currentDepthMode, depthModes, 2)) {
                g_depthSettings.usePlanarDepth = (currentDepthMode == 1);
                g_depthSettings.dirty = true;
            }

            if (ImGui::Checkbox("Invert Depth [Near = White, Far = Black] (I)", &g_depthSettings.invertDepth)) {
                g_depthSettings.dirty = true;
            }

            if (ImGui::Checkbox("Auto-Range Normalization [minHit : maxHit]", &g_depthSettings.autoRange)) {
                g_depthSettings.dirty = true;
            }

            if (g_depthSettings.autoRange) {
                ImGui::Text("Detected Range: [%.3f, %.3f]", g_depthSettings.detectedMinDepth, g_depthSettings.detectedMaxDepth);
            } else {
                if (ImGui::DragFloat("Manual Min Depth", &g_depthSettings.manualMin, 0.1f, 0.01f, 100.0f)) {
                    g_depthSettings.dirty = true;
                }
                if (ImGui::DragFloat("Manual Max Depth", &g_depthSettings.manualMax, 0.1f, 0.01f, 200.0f)) {
                    g_depthSettings.dirty = true;
                }
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
                g_depthSettings.dirty = true;
            }
        }

        // 6. Help & Keybindings
        if (ImGui::CollapsingHeader("Keyboard & Mouse Controls")) {
            ImGui::BulletText("Left Mouse Drag: Orbit Camera");
            ImGui::BulletText("Mouse Scroll: Zoom Camera");
            ImGui::BulletText("W, A, S, D: Move Camera Position");
            ImGui::BulletText("Space: Toggle Real-time Ray Shooting");
            ImGui::BulletText("B: Switch Backend (CPU <-> CUDA Zero-Copy)");
            ImGui::BulletText("R: Trigger Ray Casting");
            ImGui::BulletText("I: Invert Depth Grayscale");
            ImGui::BulletText("M: Toggle Ray Distance vs Z-depth");
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

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Task 13: Shooting Rays - Depth Map (CPU & CUDA PBO Zero-Copy)", NULL, NULL);
    if (!window) {
        std::cerr << "[GLFW] Error: Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // Uncapped framerate for maximum responsiveness

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

    // Setup fullscreen quad and depth display shader
    setupScreenQuad();
    Shader screenShader("../../shaders/screen_quad.vert", "../../shaders/screen_quad.frag");

    // Check CUDA availability
#if HAVE_CUDA
    g_cudaAvailable = CudaRayTracer::isAvailable();
    if (g_cudaAvailable) {
        g_cudaDeviceName = CudaRayTracer::getDeviceName();
        std::cout << "[CUDA] Initialized GPU: " << g_cudaDeviceName << std::endl;
        // Default to CUDA for maximum performance
        g_backend = ComputeBackend::CUDA;
    } else {
        std::cout << "[CUDA] No CUDA device detected, defaulting to CPU." << std::endl;
        g_backend = ComputeBackend::CPU;
    }
#else
    std::cout << "[App] Built in CPU-only mode." << std::endl;
    g_backend = ComputeBackend::CPU;
#endif

    // Setup Pixel Buffer Object (PBO)
    setupPBO(g_depthSettings.renderWidth, g_depthSettings.renderHeight);

    // Load initial scene
    std::string initialScene = "../../models/teapot.obj";
    if (argc > 1) {
        initialScene = argv[1];
        strncpy(g_customScenePath, argv[1], sizeof(g_customScenePath) - 1);
        g_selectedSceneIndex = 4;
    }
    loadScene(initialScene);

    // Initial ray cast
    renderDepthMap();

    float lastFrameTime = static_cast<float>(glfwGetTime());

    // Main Render Loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrameTime = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        process_input(window, deltaTime);

        // Perform ray shooting if real-time continuous mode is active or settings changed
        if (g_depthSettings.continuous && g_depthSettings.dirty) {
            renderDepthMap();
        } else if (!g_depthSettings.continuous && g_depthSettings.dirty) {
            renderDepthMap();
        }

        // Render OpenGL Screen Quad with Depth Texture
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

#if HAVE_CUDA
    g_cudaRayTracer.cleanup();
#endif

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwTerminate();
    return 0;
}
