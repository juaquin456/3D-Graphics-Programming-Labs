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
#include "CudaRayTracer.h"

// -------------------------------------------------------------
// Global Application State & Settings
// -------------------------------------------------------------
int SCR_WIDTH = 1280;
int SCR_HEIGHT = 720;

Camera cam;
Scene scene;
std::vector<CudaTriangle> g_worldTriangles;
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
    double lastKernelTimeMs = 0.0;
    int totalRays = 0;
    int hitCount = 0;
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

    if (g_cudaAvailable) {
        g_cudaRayTracer.uploadTriangles(g_worldTriangles);
        std::cout << "[CUDA] Uploaded " << g_worldTriangles.size() << " triangles to GPU." << std::endl;
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
    g_depthSettings.dirty = true;
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
// CUDA Depth Map Renderer (Zero-Copy OpenGL PBO)
// -------------------------------------------------------------
void renderDepthMapCUDA() {
    int width = g_depthSettings.renderWidth;
    int height = g_depthSettings.renderHeight;
    g_depthSettings.totalRays = width * height;

    if (!g_cudaAvailable) return;

    glm::mat4 viewMatrix = cam.getViewMatrix();
    glm::mat4 projMatrix = cam.getProjectionMatrix();
    glm::mat4 invProj = glm::inverse(projMatrix);
    glm::mat4 invView = glm::inverse(viewMatrix);

    double cudaMs = 0.0;
    float detectedMin = 0.0f, detectedMax = 1.0f;
    int hitCount = 0;

    // Zero-copy: CUDA writes directly into the mapped OpenGL PBO inside GPU VRAM
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
        g_depthSettings.lastKernelTimeMs = cudaMs;
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
    } else {
        std::cerr << "[CUDA] Warning: Render failed." << std::endl;
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

    if (ImGui::Begin("Task 14: CUDA Shooting Rays (Depth Map)", &showGui)) {
        ImGuiIO& io = ImGui::GetIO();

        // 1. GPU Backend Status
        if (ImGui::CollapsingHeader("CUDA GPU Backend", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Device: %s", g_cudaDeviceName.c_str());
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "[Interop] Zero-Copy OpenGL PBO: ACTIVE");
            ImGui::TextDisabled("-> Direct GPU VRAM to Texture DMA. 0 CPU copies.");
            ImGui::Separator();
        }

        // 2. Performance and Ray Metrics
        if (ImGui::CollapsingHeader("Ray Casting Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("OpenGL Window: %.1f FPS (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "CUDA Kernel Time: %.2f ms", g_depthSettings.lastKernelTimeMs);

            double mrays = (g_depthSettings.lastKernelTimeMs > 0.0) 
                ? (g_depthSettings.totalRays / (g_depthSettings.lastKernelTimeMs * 1000.0)) 
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

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Task 14: CUDA Shooting Rays (Depth Map)", NULL, NULL);
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
    renderDepthMapCUDA();

    float lastFrameTime = static_cast<float>(glfwGetTime());

    // Main Render Loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrameTime = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        process_input(window, deltaTime);

        // Perform ray shooting if real-time continuous mode is active or settings changed
        if (g_depthSettings.continuous && g_depthSettings.dirty) {
            renderDepthMapCUDA();
        } else if (!g_depthSettings.continuous && g_depthSettings.dirty) {
            renderDepthMapCUDA();
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

    g_cudaRayTracer.cleanup();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwTerminate();
    return 0;
}
