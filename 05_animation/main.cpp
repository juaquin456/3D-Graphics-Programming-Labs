#include <iostream>
#include <vector>
#include <cmath>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <common/Mesh.h>
#include "linalg.h"

using namespace linalg::aliases;

const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

// Vertex Shader with MVP 4x4 matrix transformations
const char* vertexShaderSource = "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "uniform mat4 uModel;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProjection;\n"
    "uniform vec3 uColor;\n"
    "out vec3 FragPos;\n"
    "out vec3 Color;\n"
    "void main()\n"
    "{\n"
    "   vec4 worldPos = uModel * vec4(aPos, 1.0);\n"
    "   FragPos = worldPos.xyz;\n"
    "   gl_Position = uProjection * uView * worldPos;\n"
    "   Color = uColor;\n"
    "}\0";

// Fragment Shader with face normal computation & directional lighting
const char* fragmentShaderSource = "#version 330 core\n"
    "in vec3 FragPos;\n"
    "in vec3 Color;\n"
    "out vec4 FragColor;\n"
    "void main()\n"
    "{\n"
    "   vec3 normal = normalize(cross(dFdx(FragPos), dFdy(FragPos)));\n"
    "   vec3 lightDir = normalize(vec3(1.0, 2.0, 1.5));\n"
    "   float diff = max(dot(normal, lightDir), 0.0);\n"
    "   float ambient = 0.35;\n"
    "   vec3 result = (ambient + diff * 0.65) * Color;\n"
    "   FragColor = vec4(result, 1.0);\n"
    "}\n\0";

// 4x4 Transformation Matrix helper functions
float4x4 translation4x4(float tx, float ty, float tz) {
    return float4x4{
        float4{1.0f, 0.0f, 0.0f, 0.0f},
        float4{0.0f, 1.0f, 0.0f, 0.0f},
        float4{0.0f, 0.0f, 1.0f, 0.0f},
        float4{  tx,   ty,   tz, 1.0f}
    };
}

float4x4 scaling4x4(float sx, float sy, float sz) {
    return float4x4{
        float4{  sx, 0.0f, 0.0f, 0.0f},
        float4{0.0f,   sy, 0.0f, 0.0f},
        float4{0.0f, 0.0f,   sz, 0.0f},
        float4{0.0f, 0.0f, 0.0f, 1.0f}
    };
}

float4x4 rotationX4x4(float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return float4x4{
        float4{1.0f, 0.0f, 0.0f, 0.0f},
        float4{0.0f,    c,    s, 0.0f},
        float4{0.0f,   -s,    c, 0.0f},
        float4{0.0f, 0.0f, 0.0f, 1.0f}
    };
}

float4x4 rotationY4x4(float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return float4x4{
        float4{   c, 0.0f,   -s, 0.0f},
        float4{0.0f, 1.0f, 0.0f, 0.0f},
        float4{   s, 0.0f,    c, 0.0f},
        float4{0.0f, 0.0f, 0.0f, 1.0f}
    };
}

float4x4 rotationZ4x4(float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return float4x4{
        float4{   c,    s, 0.0f, 0.0f},
        float4{  -s,    c, 0.0f, 0.0f},
        float4{0.0f, 0.0f, 1.0f, 0.0f},
        float4{0.0f, 0.0f, 0.0f, 1.0f}
    };
}

float4x4 perspective4x4(float fov_rad, float aspect, float near_z, float far_z) {
    float tan_half_fov = std::tan(fov_rad / 2.0f);
    float4x4 res(0.0f);
    res[0][0] = 1.0f / (aspect * tan_half_fov);
    res[1][1] = 1.0f / tan_half_fov;
    res[2][2] = -(far_z + near_z) / (far_z - near_z);
    res[2][3] = -1.0f;
    res[3][2] = -(2.0f * far_z * near_z) / (far_z - near_z);
    return res;
}

float4x4 lookAt4x4(const float3& eye, const float3& center, const float3& up) {
    float3 f = normalize(center - eye);
    float3 s = normalize(cross(f, up));
    float3 u = cross(s, f);

    float4x4 res(0.0f);
    res[0] = float4{ s.x,  u.x, -f.x, 0.0f};
    res[1] = float4{ s.y,  u.y, -f.y, 0.0f};
    res[2] = float4{ s.z,  u.z, -f.z, 0.0f};
    res[3] = float4{-dot(s, eye), -dot(u, eye), dot(f, eye), 1.0f};
    return res;
}

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

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "05_animation - Cube & Orbiting Sphere", NULL, NULL);
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

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
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

    int modelLoc = glGetUniformLocation(shaderProgram, "uModel");
    int viewLoc  = glGetUniformLocation(shaderProgram, "uView");
    int projLoc  = glGetUniformLocation(shaderProgram, "uProjection");
    int colorLoc = glGetUniformLocation(shaderProgram, "uColor");

    Mesh cube = NewCube(1.0f);
    unsigned int cubeVAO, cubeVBO, cubeEBO;
    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);
    glGenBuffers(1, &cubeEBO);

    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * cube.vertices.size(), cube.vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(int) * cube.indices.size(), cube.indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    Mesh sphere = NewSphere(0.4f, 30, 30);
    unsigned int sphereVAO, sphereVBO, sphereEBO;
    glGenVertexArrays(1, &sphereVAO);
    glGenBuffers(1, &sphereVBO);
    glGenBuffers(1, &sphereEBO);

    glBindVertexArray(sphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * sphere.vertices.size(), sphere.vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(int) * sphere.indices.size(), sphere.indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    float cubeRotationAngle = 0.0f;
    float sphereOrbitAngle = 0.0f;

    float4x4 projection = perspective4x4(45.0f * (3.14159265f / 180.0f),
                                         (float)SCR_WIDTH / (float)SCR_HEIGHT,
                                         0.1f, 100.0f);

    float4x4 view = lookAt4x4(float3{0.0f, 3.0f, 6.0f},
                              float3{0.0f, 0.0f, 0.0f},
                              float3{0.0f, 1.0f, 0.0f});

    while (!glfwWindowShouldClose(window)) {
        process_input(window);

        cubeRotationAngle += 0.02f;
        sphereOrbitAngle += 0.015f;

        glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &view[0][0]);
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, &projection[0][0]);

        float4x4 cubeModel = mul(rotationY4x4(cubeRotationAngle), rotationX4x4(0.2f));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &cubeModel[0][0]);
        glUniform3f(colorLoc, 0.95f, 0.45f, 0.2f);

        glBindVertexArray(cubeVAO);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cube.indices.size()), GL_UNSIGNED_INT, 0);

        float orbitRadius = 2.4f;
        float4x4 sphereOrbit = mul(rotationY4x4(sphereOrbitAngle),
                                   translation4x4(orbitRadius, 0.0f, 0.0f));
        float4x4 sphereModel = mul(sphereOrbit, rotationY4x4(sphereOrbitAngle * 2.0f));

        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &sphereModel[0][0]);
        glUniform3f(colorLoc, 0.2f, 0.7f, 0.95f); // Cyan / Blue color

        glBindVertexArray(sphereVAO);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sphere.indices.size()), GL_UNSIGNED_INT, 0);

        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup resources
    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteBuffers(1, &cubeVBO);
    glDeleteBuffers(1, &cubeEBO);

    glDeleteVertexArrays(1, &sphereVAO);
    glDeleteBuffers(1, &sphereVBO);
    glDeleteBuffers(1, &sphereEBO);

    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}
