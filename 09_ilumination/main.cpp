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


const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;


void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
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
    glEnable(GL_DEPTH_TEST);

    Shader baseShader("../shader.vert", "../shader.frag");

    int viewLoc  = glGetUniformLocation(baseShader.ID, "uView");
    int projLoc  = glGetUniformLocation(baseShader.ID, "uProjection");
    int modelLoc = glGetUniformLocation(baseShader.ID, "uModel");

    MeshData dragonData = readPly("../../models/dragon.ply");
    auto dragonAsset = std::make_shared<MeshAsset>(dragonData);

    RenderObject dragonL(dragonAsset);
    Camera cam;

    const float radius = 2.0f;
    const float camY = 0.5f;
    while (!glfwWindowShouldClose(window)) {
        process_input(window);
        float time = static_cast<float>(glfwGetTime());

        float camX = std::sin(0.8 * time) * radius;
        float camZ = std::cos(0.8*time) * radius;

        cam.position =  glm::vec3{camX, camY, camZ};

        glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        baseShader.use();

        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(cam.getViewMatrix()));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(cam.getProjectionMatrix()));

        dragonL.draw(modelLoc);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}