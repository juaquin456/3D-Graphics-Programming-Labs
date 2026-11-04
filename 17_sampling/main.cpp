#include <algorithm>
#include <iostream>
#include <vector>
#include <cmath>
#include <filesystem>
#include <chrono>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

#include "common/Camera.h"
#include "common/MeshAsset.h"
#include "common/MeshData.h"
#include "common/RenderObject.h"
#include "common/Scene.h"
#include "common/Shader.h"
#include "common/ShadowMap.h"

#include "gui/GuiManager.h"
#include "sampling/SamplingGenerators.h"
#include "sampling/PointCloudBuilder.h"

int SCR_WIDTH = 1360;
int SCR_HEIGHT = 768;

Camera cam;
Scene s;
EngineState engineState;
Sampling::SobolGenerator sobolGen;

bool isDragging = false;
double lastMouseX = 0, lastMouseY = 0;

std::string findFile(const std::vector<std::string>& candidates) {
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    std::cerr << "[Warning] Could not locate file among candidates:\n";
    for (const auto& path : candidates) {
        std::cerr << "  - " << path << "\n";
    }
    return candidates.empty() ? "" : candidates[0];
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    if (width <= 0 || height <= 0) return;
    glViewport(0, 0, width, height);
    SCR_WIDTH = width;
    SCR_HEIGHT = height;
    cam.aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
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

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (ImGui::GetIO().WantCaptureMouse) return;

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
    if (isDragging) {
        float deltaX = static_cast<float>(xpos - lastMouseX);
        float deltaY = static_cast<float>(ypos - lastMouseY);

        lastMouseX = xpos;
        lastMouseY = ypos;

        cam.rotateOrbit(deltaX, deltaY, 0.005f);
    }
}

void process_input(GLFWwindow* window, float deltaTime) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (!ImGui::GetIO().WantCaptureKeyboard) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cam.moveForward(deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cam.moveBackward(deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cam.moveLeft(deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cam.moveRight(deltaTime);
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

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Lab 17: Halton & Sobol Point Cloud Generator", NULL, NULL);
    if (!window) {
        std::cerr << "[GLFW] Error: Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    glfwSwapInterval(1); // VSync
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
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

    std::string shadowVert = findFile({
        "../../shaders/shadow_depth.vert",
        "../shaders/shadow_depth.vert",
        "shaders/shadow_depth.vert"
    });
    std::string shadowFrag = findFile({
        "../../shaders/shadow_depth.frag",
        "../shaders/shadow_depth.frag",
        "shaders/shadow_depth.frag"
    });

    std::string pcVert = findFile({
        "shaders/point_cloud.vert",
        "17_sampling/shaders/point_cloud.vert",
        "../shaders/point_cloud.vert",
        "../../17_sampling/shaders/point_cloud.vert"
    });
    std::string pcFrag = findFile({
        "shaders/point_cloud.frag",
        "17_sampling/shaders/point_cloud.frag",
        "../shaders/point_cloud.frag",
        "../../17_sampling/shaders/point_cloud.frag"
    });

    Shader depthShader(shadowVert.c_str(), shadowFrag.c_str());
    Shader pointCloudShader(pcVert.c_str(), pcFrag.c_str());

    DirectionalLight light;
    light.ambient = glm::vec3(0.4f);
    light.diffuse = glm::vec3(0.85f);
    light.specular = glm::vec3(0.9f);
    light.position = glm::vec3(2.5f, 5.0f, 3.5f);

    ShadowMap shadowMap;
    shadowMap.init(2048, 2048);

    cam.position = glm::vec3(0.0f, 1.0f, 2.8f);
    cam.target = glm::vec3(0.0f, 0.0f, 0.0f);
    cam.aspect_ratio = static_cast<float>(SCR_WIDTH) / static_cast<float>(SCR_HEIGHT);

    float lastFrameTime = glfwGetTime();

    PointCloudBuilder::updateScene(
        s,
        engineState.samplingConfig,
        sobolGen,
        engineState.metricsSingle,
        engineState.metricsRandom,
        engineState.metricsHalton,
        engineState.metricsSobol
    );
    engineState.sceneNeedsRebuild = false;

    while (!glfwWindowShouldClose(window)) {
        float currentFrameTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentFrameTime - lastFrameTime);
        lastFrameTime = currentFrameTime;

        if (engineState.sceneNeedsRebuild) {
            PointCloudBuilder::updateScene(
                s,
                engineState.samplingConfig,
                sobolGen,
                engineState.metricsSingle,
                engineState.metricsRandom,
                engineState.metricsHalton,
                engineState.metricsSobol
            );
            engineState.sceneNeedsRebuild = false;
        }

        process_input(window, deltaTime);

        if (engineState.animateLight) {
            light.position.x = 4.0f * std::cos(currentFrameTime * 0.8f);
            light.position.z = 4.0f * std::sin(currentFrameTime * 0.8f);
        }

        if (engineState.useShadowMap) {
            shadowMap.bindForWriting();
            depthShader.use();
            glCullFace(GL_FRONT);
            depthShader.setMat4("lightSpaceMatrix", light.getLightSpaceMatrix(6.0f, 1.0f, 20.0f));
            s.drawGeometry(depthShader);
            shadowMap.unbind(SCR_WIDTH, SCR_HEIGHT);
        }

        glClearColor(0.11f, 0.13f, 0.16f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glCullFace(GL_BACK);
        shadowMap.bindTexture(3);

        pointCloudShader.use();
        pointCloudShader.setBool("useShadows", engineState.useShadowMap);
        pointCloudShader.setBool("useVertexColor", engineState.samplingConfig.colorMode != ColorMode::Uniform);
        pointCloudShader.setInt("shadowMap", 3);
        pointCloudShader.setVec3("viewPos", cam.position);
        pointCloudShader.setMat4("lightSpaceMatrix", light.getLightSpaceMatrix(6.0f, 1.0f, 20.0f));

        cam.bind(pointCloudShader);
        light.bind(pointCloudShader);

        s.draw(pointCloudShader);

        gui.render(engineState, light, cam);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    gui.shutdown();
    glfwTerminate();
    return 0;
}
