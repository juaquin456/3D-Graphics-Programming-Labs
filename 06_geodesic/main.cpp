#include <algorithm>
#include <iostream>
#include <vector>
#include <cmath>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <common/Mesh.h>
#include "linalg.h"
#include <queue>
#include <limits>
#include <utility>
#include <fstream>
#include <sstream>

#include "common/HalfEdgeMesh.h"

using namespace linalg::aliases;

const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

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

        linalg::aliases::float3 pos_u = mesh.get_vertex_pos(u);

        for (int v : mesh.get_neighbors(u)) {
            linalg::aliases::float3 pos_v = mesh.get_vertex_pos(v);

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

float update_triangle(const float3& x0, const float3& x1, const float3& x2, float t1, float t2) {
    float3 v1 = x1 - x0;
    float3 v2 = x2 - x0;

    linalg::mat<float, 3, 2> X{v1, v2};

    auto E = mul(transpose(X), X);

    float det = determinant(E);
    if (det <= 1e-8f) {
        return std::min(t1 + std::sqrt(E[0][0]), t2 + std::sqrt(E[1][1]));
    }

    auto Q = inverse(E);

    float q11 = Q[0][0], q12 = Q[0][1];
    float q21 = Q[1][0], q22 = Q[1][1];

    linalg::vec<float, 2> one{1.0f, 1.0f};
    linalg::vec<float, 2> T{t1, t2};

    float a = dot(one, mul(Q, one));
    float b = dot(one, mul(Q, T));
    float c = dot(T, mul(Q, T)) - 1.0f;

    float disc = b * b - a * c;
    float t0 = std::numeric_limits<float>::infinity();

    if (disc >= 0.0f) {
        float t0_candidate = (b + std::sqrt(disc)) / a;

        float cond1 = q11 * (t1 - t0_candidate) + q12 * (t2 - t0_candidate);
        float cond2 = q21 * (t1 - t0_candidate) + q22 * (t2 - t0_candidate);

        if (cond1 < 0.0f && cond2 < 0.0f && t0_candidate > std::max(t1, t2)) {
            t0 = t0_candidate;
        }
    }

    if (t0 == std::numeric_limits<float>::infinity()) {
        t0 = std::min(t1 + std::sqrt(E[0][0]), t2 + std::sqrt(E[1][1]));
    }

    return t0;
}

std::vector<float> compute_fast_marching_distances(const HalfEdgeContainer& mesh, int start_vertex) {
    int n = mesh.n_vertices();
    std::vector<float> distances(n, std::numeric_limits<float>::infinity());
    std::vector<bool> vis(n, false);
    using pii = std::pair<float, int>;
    std::priority_queue<pii, std::vector<pii>, std::greater<pii>> pq;

    distances[start_vertex] = 0.0f;
    pq.push({0.0f, start_vertex});

    auto update_vertex_eikonal = [&](int u) {
        if (vis[u]) return;

        float min_dist = distances[u];
        float3 pos_u = mesh.get_vertex_pos(u);

        int start_he = mesh.vertex_to_he[u];
        if (start_he == -1) return;

        int curr_he = start_he;
        do {
            int next_he = next(curr_he);
            int prev_he = prev(curr_he);

            int v1 = mesh.he_to_vertex[next_he];
            int v2 = mesh.he_to_vertex[prev_he];

            float3 pos_v1 = mesh.get_vertex_pos(v1);
            float3 pos_v2 = mesh.get_vertex_pos(v2);

            float t1 = distances[v1];
            float t2 = distances[v2];

            if (t1 != std::numeric_limits<float>::infinity() || t2 != std::numeric_limits<float>::infinity()) {
                float candidate_t = update_triangle(pos_u, pos_v1, pos_v2, t1, t2);
                min_dist = std::min(min_dist, candidate_t);
            }

            int twin_he = mesh.twin[prev_he];
            if (twin_he == -1) break;
            curr_he = twin_he;

        } while (curr_he != start_he);

        if (min_dist < distances[u]) {
            distances[u] = min_dist;
            pq.push({min_dist, u});
        }
    };

    while (!pq.empty()) {
        auto [current_dist, u] = pq.top();
        pq.pop();

        if (vis[u]) continue;
        vis[u] = true;

        for (int v : mesh.get_neighbors(u)) {
            if (!vis[v]) {
                update_vertex_eikonal(v);
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

    Mesh m("../../models/dragon.ply");
    auto [l, u] = m.bounding_box();
    float3 center = (l + u) * 0.5f;
    float3 extent = u - l;

    float max_extent = std::max({extent.x, extent.y, extent.z});
    float scale_factor = 1.0f / max_extent;

    HalfEdgeContainer he = NewHalfEdgeContainer(m);
    auto vertex_distances = compute_fast_marching_distances(he, 10000);

    unsigned int VAO, VBO[2], EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(2, VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO[0]);
    glBufferData(GL_ARRAY_BUFFER, he.vertices.size() * sizeof(float), he.vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, VBO[1]);
    glBufferData(GL_ARRAY_BUFFER, vertex_distances.size() * sizeof(float), vertex_distances.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 1 * sizeof(float), (void*)0); // size = 1
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, he.he_to_vertex.size() * sizeof(int), he.he_to_vertex.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);



    float4x4 projection = perspective4x4(45.0f * (3.14159265f / 180.0f),
                                         (float)SCR_WIDTH / (float)SCR_HEIGHT,
                                         0.1f, 100.0f);

    float4x4 view = lookAt4x4(float3{0.0f, 0.5f, 2.f},
                              float3{0.0f, 0.0f, 0.0f},
                              float3{0.0f, 1.0f, 0.0f});

    float4x4 model = mul(scaling4x4(scale_factor, scale_factor, scale_factor),
                         translation4x4(-center.x, -center.y, -center.z));

    while (!glfwWindowShouldClose(window)) {
        process_input(window);


        glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &view[0][0]);
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, &projection[0][0]);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &model[0][0]);

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, he.he_to_vertex.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}