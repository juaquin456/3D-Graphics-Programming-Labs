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


const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;
bool isDragging = false;
glm::vec3 lastMouseVec(0.0f);
Camera cam;

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            isDragging = true;
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);

            lastMouseVec = Camera::getArcballVector((int)mouseX, (int)mouseY, SCR_WIDTH, SCR_HEIGHT);
        } else if (action == GLFW_RELEASE) {
            isDragging = false;
        }
    }
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    if (!isDragging) return;

    glm::vec3 currentMouseVec = Camera::getArcballVector((int)xpos, (int)ypos, SCR_WIDTH, SCR_HEIGHT);

    cam.updateArcball(lastMouseVec, currentMouseVec);

    lastMouseVec = currentMouseVec;
}

std::string readShaderCode(const char* filePath) {
    std::string shaderCode;
    std::ifstream shaderFile;

    shaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    try {
        shaderFile.open(filePath);
        std::stringstream shaderStream;

        shaderStream << shaderFile.rdbuf();
        shaderFile.close();

        shaderCode = shaderStream.str();
    }
    catch (std::ifstream::failure& e) {
        std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ: " << filePath << std::endl;
    }

    return shaderCode;
}

const char* fragmentShaderSource = "#version 330 core\n"
    "in vec3 FragPos;\n"
    "in vec4 Color;\n"
    "out vec4 FragColor;\n"
    "void main()\n"
    "{\n"
    "   vec3 normal = normalize(cross(dFdx(FragPos), dFdy(FragPos)));\n"
    "   vec3 lightDir = normalize(vec3(1.0, 2.0, 1.5));\n"
    "   float diff = max(dot(normal, lightDir), 0.0);\n"
    "   float ambient = 0.35;\n"
    "   vec3 result = (ambient + diff * 0.65) * Color.rgb;\n"
    "   FragColor = vec4(result, 1.0);\n"
    "}\n\0";


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

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "07_placing_camera - Placing Camera", NULL, NULL);
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
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    auto vertexShaderSource = readShaderCode("../shader.frag");
    const char* vertexShaderChars = vertexShaderSource.c_str();
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderChars, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    int viewLoc  = glGetUniformLocation(shaderProgram, "uView");
    int projLoc  = glGetUniformLocation(shaderProgram, "uProjection");
    int modelLoc = glGetUniformLocation(shaderProgram, "uModel");

    MeshData dragonData = readPly("../../models/dragon.ply");
    auto dragonAsset = std::make_shared<MeshAsset>(dragonData);

    RenderObject dragonL(dragonAsset);
    dragonL.position = {0, 0, 0};
    dragonL.scale = glm::vec3(1);
    while (!glfwWindowShouldClose(window)) {
        process_input(window);
        glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(cam.getViewMatrix()));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(cam.getProjectionMatrix()));

        dragonL.draw(modelLoc);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}