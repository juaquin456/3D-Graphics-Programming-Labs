#include <algorithm>
#include <iostream>
#include <vector>
#include <cmath>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <common/MeshData.h>
#include <common/MeshAsset.h>
#include <common/RenderObject.h>
#include <common/Camera.h>
#include <queue>
#include <limits>
#include <utility>
#include <fstream>
#include <sstream>
#include <glm/glm.hpp>

#include "common/HalfEdgeMesh.h"
#include "glm/gtc/type_ptr.hpp"


const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

bool isDragging = false;
double lastMouseX = 0.0;
double lastMouseY = 0.0;

Camera cam;

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
    if (!isDragging) return;

    float deltaX = static_cast<float>(xpos - lastMouseX);
    float deltaY = static_cast<float>(ypos - lastMouseY);

    lastMouseX = xpos;
    lastMouseY = ypos;

    cam.rotateOrbit(deltaX, deltaY, 0.005);
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


std::vector<float> compute_dijkstra_distances(const HalfEdgeContainer& mesh, int start_vertex) {
    int n = mesh.n_vertices();
    std::vector<float> distances(n, std::numeric_limits<float>::infinity());

    using pii = std::pair<float, int>;
    std::priority_queue<pii, std::vector<pii>, std::greater<pii>> pq;

    distances[start_vertex] = 0.0f;
    pq.push({0.0f, start_vertex});

    while (!pq.empty()) {
        auto [current_dist, u] = pq.top();
        pq.pop();

        if (current_dist > distances[u]) {
            continue;
        }

       glm::vec3 pos_u = mesh.get_vertex_pos(u);

        for (int v : mesh.get_neighbors(u)) {
            glm::vec3 pos_v = mesh.get_vertex_pos(v);

            float dx = pos_v.x - pos_u.x;
            float dy = pos_v.y - pos_u.y;
            float dz = pos_v.z - pos_u.z;
            float weight = std::sqrt(dx * dx + dy * dy + dz * dz);

            float new_dist = distances[u] + weight;

            if (new_dist < distances[v]) {
                distances[v] = new_dist;
                pq.push({new_dist, v});
            }
        }
    }

    return distances;
}


int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "06_geodesic - Fast Marching", NULL, NULL);
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
    HalfEdgeContainer he = NewHalfEdgeContainer(dragonData);
    auto vertex_distances = compute_fast_marching_distances(he, 1000);
    float mx_dst = std::numeric_limits<float>::lowest();
    for (float vertex_distance : vertex_distances) {
        if (vertex_distance != std::numeric_limits<float>::infinity()) {
            mx_dst = std::max(mx_dst, vertex_distance);
        }
    }
    std::cerr << "vertex_distances: " << mx_dst << std::endl;
    for (float & vertex_distance : vertex_distances) {
        vertex_distance /= mx_dst;
        dragonData.uvs.emplace_back(vertex_distance, vertex_distance);
    }

    auto dragonAsset = std::make_shared<MeshAsset>(dragonData);
    RenderObject dragon(dragonAsset);
    dragon.position = glm::vec3(0.0f, 0.0f, 0.0f);
    dragon.scale = glm::vec3(1.0f, 1.0f, 1.0f);

    while (!glfwWindowShouldClose(window)) {
        process_input(window);

        glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(cam.getViewMatrix()));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(cam.getProjectionMatrix()));

        dragon.draw(modelLoc);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}