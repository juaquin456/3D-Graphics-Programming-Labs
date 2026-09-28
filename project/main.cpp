#include <algorithm>
#include <iostream>
#include <vector>
#include <cmath>
#include <utility>
#include <fstream>
#include <sstream>
#include <chrono>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

#include "common/Camera.h"
#include "common/HalfEdgeMesh.h"
#include "common/MeshAsset.h"
#include "common/MeshData.h"
#include "common/RenderObject.h"
#include "common/Scene.h"
#include "common/Shader.h"
#include "common/ShadowMap.h"
#include "common/MeshSimplifier.h"

#include "gui/GuiManager.h"

int SCR_WIDTH = 1280;
int SCR_HEIGHT = 720;

Camera cam;
Scene s;
EngineState engineState;

bool isDragging = false;
double lastMouseX = 0, lastMouseY = 0;

void triggerFastMarchingOnPick(PickResult& pick) {
    if (!pick.hit || !pick.meshAsset) return;

    auto start = std::chrono::high_resolution_clock::now();

    MeshData& meshData = pick.meshAsset->mesh;

    HalfEdgeContainer he = GeometryUtils::buildHalfEdge(meshData);

    std::vector<float> distances = he.compute_fast_marching_distances(pick.vertexIndex);

    const float INF = std::numeric_limits<float>::infinity();

    float maxDist = 0.0f;
    for (float d : distances) {
        if (d != INF && !std::isinf(d) && d > maxDist) {
            maxDist = d;
        }
    }

    if (maxDist <= std::numeric_limits<float>::epsilon()) {
        maxDist = 1.0f;
    }

    if (meshData.uvs.size() != meshData.vertices.size()) {
        meshData.uvs.resize(meshData.vertices.size(), glm::vec2(0.0f));
    }

    for (size_t i = 0; i < meshData.vertices.size(); ++i) {
        if (distances[i] == INF || std::isinf(distances[i])) {
            meshData.uvs[i].x = 1.0f;
        } else {
            meshData.uvs[i].x = distances[i] / maxDist;
        }
    }
    pick.meshAsset->updateGPU();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;

    std::cout << "[Fast Marching] Computed distance field from seed vertex ID: "
              << pick.vertexIndex << " (" << meshData.vertices.size() << " vertices) | Time: "
              << duration.count() << " ms (Max Dist: " << maxDist << ")" << std::endl;
}

void triggerMeshSimplification(std::shared_ptr<MeshAsset>& meshAsset, float targetRatio) {
    if (!meshAsset) return;

    MeshData baseMesh = meshAsset->originalMesh;

    int totalTriangles = static_cast<int>(baseMesh.indices.size() / 3);
    int targetTriangles = static_cast<int>(totalTriangles * targetRatio);

    if (targetTriangles >= totalTriangles) {
        meshAsset->restoreOriginal();
        return;
    }

    int trianglesToRemove = totalTriangles - targetTriangles;
    int edgesToRemove = trianglesToRemove / 2;

    auto start = std::chrono::high_resolution_clock::now();

    HalfEdgeContainer he = GeometryUtils::buildHalfEdge(baseMesh);
    MeshSimplifier simplifier(he);

    simplifier.simplify(edgesToRemove);

    MeshData simplifiedMesh = GeometryUtils::buildMeshData(he);
    simplifiedMesh.recompute_normals();
    simplifiedMesh.recompute_tangents();

    meshAsset->mesh = simplifiedMesh;
    meshAsset->updateGPU();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;

    int newTriangles = static_cast<int>(simplifiedMesh.indices.size() / 3);
    float reductionRatio = (1.0f - static_cast<float>(newTriangles) / totalTriangles) * 100.0f;

    std::cout << "--------------------------------------------------" << std::endl;
    std::cout << "[QEM Simplifier] Execution Time: " << duration.count() << " ms" << std::endl;
    std::cout << "[QEM Simplifier] Triangles: " << totalTriangles
              << " -> " << newTriangles << " (" << reductionRatio << "% reduced)" << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
    glViewport(0, 0, width, height);
    SCR_WIDTH = width;
    SCR_HEIGHT = height;
    cam.aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
}

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
    if (ImGui::GetIO().WantCaptureMouse) return;

    glm::vec3 viewDir = cam.position - cam.target;
    float currentDist = glm::length(viewDir);

    float zoomSensitivity = 0.15f;
    float newDist = currentDist - static_cast<float>(yoffset) * zoomSensitivity;
    if (newDist < 0.1f) newDist = 0.1f;

    if (currentDist > 1e-5f) {
        cam.position = cam.target + glm::normalize(viewDir) * newDist;
    }
}

void mouse_button_callback(GLFWwindow *window, int button, int action, int mods) {
    if (ImGui::GetIO().WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            isDragging = true;
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        } else if (action == GLFW_RELEASE) {
            isDragging = false;
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);

            Ray mouseRay = Ray::GetMouseRay(mouseX, mouseY, SCR_WIDTH, SCR_HEIGHT,
                                           cam.getViewMatrix(), cam.getProjectionMatrix());

            PickResult pick = s.pickVertex(mouseRay);

            if (pick.hit) {
                engineState.lastPickResult = pick;
                engineState.hasSelection = true;

                std::cout << "[Picking] Selected Vertex ID: " << pick.vertexIndex
                          << " in sub-mesh (" << pick.meshAsset.get() << ")\n"
                          << "          World Position: (" << pick.vertexWorldPos.x << ", "
                          << pick.vertexWorldPos.y << ", " << pick.vertexWorldPos.z << ")" << std::endl;

                triggerFastMarchingOnPick(pick);
            }
        }
    }
}

void cursor_position_callback(GLFWwindow *window, double xpos, double ypos) {
    if (isDragging) {
        float deltaX = static_cast<float>(xpos - lastMouseX);
        float deltaY = static_cast<float>(ypos - lastMouseY);

        lastMouseX = xpos;
        lastMouseY = ypos;

        cam.rotateOrbit(deltaX, deltaY, 0.005f);
    }
}

void process_input(GLFWwindow *window, float deltaTime) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            cam.moveForward(deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            cam.moveBackward(deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            cam.moveLeft(deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            cam.moveRight(deltaTime);
    }
}

int main() {
    if (!glfwInit()) {
        std::cerr << "[GLFW] Error: Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "3D Scene Viewer Engine", NULL, NULL);
    if (!window) {
        std::cerr << "[GLFW] Error: Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    glfwSwapInterval(0);
    if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
        std::cerr << "[GLAD] Error: Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glEnable(GL_DEPTH_TEST);

    GuiManager gui;
    gui.init(window);

    Shader depthShader("../../shaders/shadow_depth.vert", "../../shaders/shadow_depth.frag");
    Shader solidShader("../../shaders/texture.vert", "../../shaders/phong_solid.frag");
    Shader texturedShader("../../shaders/texture.vert", "../../shaders/phong_textured.frag");
    Shader flatShader("../../shaders/debug_simple.vert", "../../shaders/debug_flat.frag");
    Shader uvHeatmapShader("../../shaders/debug_simple.vert", "../../shaders/debug_uv_heatmap.frag");

    DirectionalLight light;
    light.ambient = glm::vec3(0.35f);
    light.diffuse = glm::vec3(0.8f);
    light.specular = glm::vec3(1.0f);
    light.position = glm::vec3(2.0f, 6.0f, 3.0f);

    ShadowMap shadowMap;
    shadowMap.init(2048, 2048);

    float lastFrameTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        float currentFrameTime = glfwGetTime();
        auto deltaTime = static_cast<float>(currentFrameTime - lastFrameTime);

        lastFrameTime = currentFrameTime;
        if (engineState.triggerSimplification) {
            if (engineState.lastPickResult.meshAsset) {
                triggerMeshSimplification(
                    engineState.lastPickResult.meshAsset,
                    engineState.simplificationRatio
                );
            }
            engineState.triggerSimplification = false;
        }

        if (engineState.triggerResetMesh) {
            if (engineState.lastPickResult.meshAsset) {
                engineState.lastPickResult.meshAsset->restoreOriginal();
            }
            engineState.triggerResetMesh = false;
        }

        if (engineState.triggerLoadOBJ) {
            std::cout << "[Scene] Replacing current scene with: " << engineState.objFilePath << std::endl;

            engineState.hasSelection = false;
            engineState.lastPickResult = PickResult{};

            Scene newScene = Scene::loadOBJ(engineState.objFilePath);

            if (!newScene.objects.empty()) {
                s = std::move(newScene);
                std::cout << "[Scene] Scene loaded successfully." << std::endl;
            } else {
                std::cerr << "[Scene] Error: Failed to load file or scene is empty." << std::endl;
            }

            engineState.triggerLoadOBJ = false;
        }

        // 1. Input & Procedural Light Animation Update
        process_input(window, deltaTime);

        if (engineState.animateLight) {
            light.position.x = 4.0f * std::cos(currentFrameTime);
            light.position.z = 4.0f * std::sin(currentFrameTime);
        }

        // Pass 1: Shadow Map Depth Pass
        if (engineState.useShadowMap) {
            shadowMap.bindForWriting();
            depthShader.use();
            glCullFace(GL_FRONT);
            depthShader.setMat4("lightSpaceMatrix", light.getLightSpaceMatrix());
            s.drawGeometry(depthShader);
            shadowMap.unbind(SCR_WIDTH, SCR_HEIGHT);
        }

        // Pass 2: Main Render Pass
        glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        shadowMap.bindTexture(3);

        glCullFace(GL_BACK);

        if (engineState.debugMode == 1) {
            flatShader.use();
            cam.bind(flatShader);
            flatShader.setVec3("u_FlatColor", glm::vec3(0.75f));
            s.drawGeometry(flatShader);
        } else if (engineState.debugMode == 2) {
            uvHeatmapShader.use();
            cam.bind(uvHeatmapShader);
            uvHeatmapShader.setBool("u_IsHeatmapMode", engineState.isHeatmapMode);
            s.drawGeometry(uvHeatmapShader);
        } else {
            // Mode 0: Default Phong
            solidShader.use();
            solidShader.setBool("useShadows", engineState.useShadowMap);
            cam.bind(solidShader);
            light.bind(solidShader);
            solidShader.setInt("shadowMap", 3);
            s.drawSolid(solidShader);

            texturedShader.use();
            texturedShader.setBool("useShadows", engineState.useShadowMap);
            cam.bind(texturedShader);
            light.bind(texturedShader);
            texturedShader.setInt("shadowMap", 3);
            s.drawTextured(texturedShader);
        }

        // Pass 3: GUI Render Pass
        gui.render(engineState, light, cam);

        // Buffer Swap & Event Polling
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    gui.shutdown();
    glfwTerminate();
    return 0;
}