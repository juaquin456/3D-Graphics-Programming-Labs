#include <algorithm>
#include <iostream>
#include <vector>
#include <cmath>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <common/MeshData.h>
#include <utility>
#include <fstream>
#include <sstream>

#include "common/Camera.h"
#include "glm/fwd.hpp"
#include <glm/gtc/type_ptr.hpp>

#include "common/MeshAsset.h"
#include "common/RenderObject.h"
#include "common/Shader.h"
#include "common/ShadowMap.h"


const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

Camera cam;
bool isDragging = false;
double lastMouseX = 0, lastMouseY = 0;


void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    cam.aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
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

void process_input(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

}
int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "09_ilumination - Ilumination", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glEnable(GL_DEPTH_TEST);


    Shader depthShader("../../shaders/shadow_depth.vert", "../../shaders/shadow_depth.frag");
    Shader sceneShader("../../shaders/shadow_phong.vert", "../../shaders/shadow_phong.frag");

    DirectionalLight light;

    ShadowMap shadowMap;
    shadowMap.init(2048, 2048);

    auto planeAsset = std::make_shared<MeshAsset>(MeshData::NewCube(1));
    RenderObject floor(planeAsset);
    floor.scale = glm::vec3(10, 0.1, 10);
    floor.position = glm::vec3(0.0f, -0.5, 0.0f);
    floor.material = Material::Silver();


    auto bunnyAsset = std::make_shared<MeshAsset>(MeshData::readPly("../../models/bunny.ply"));
    RenderObject bunny(bunnyAsset);
    bunny.position = glm::vec3(0.0f, 0.5, 0.0f);
    bunny.material = Material::Gold();
    float lastFrameTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        process_input(window);

        float currentFrameTime = glfwGetTime();
        auto deltaTime = static_cast<float>(currentFrameTime - lastFrameTime);
        lastFrameTime = currentFrameTime;

        bunny.rotateAxis(30 * deltaTime, glm::vec3(0.0f, 1.0f, 0.0f));
        light.position.x = glm::sin(currentFrameTime)*5;

        shadowMap.bindForWriting();
        depthShader.use();
        glCullFace(GL_FRONT);
        depthShader.setMat4("lightSpaceMatrix", light.getLightSpaceMatrix());
        floor.drawGeometry(depthShader);
        bunny.drawGeometry(depthShader);

        shadowMap.unbind(SCR_WIDTH, SCR_HEIGHT);

        glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        sceneShader.use();
        glCullFace(GL_BACK);
        cam.bind(sceneShader);
        light.bind(sceneShader);
        shadowMap.bindTexture(1);
        sceneShader.setInt("shadowMap", 1);

        floor.draw(sceneShader);
        bunny.draw(sceneShader);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}