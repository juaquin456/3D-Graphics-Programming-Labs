#include <iostream>
#include <ostream>
#include <vector>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

const int MAX_ITERATIONS = 10000;
const int MAX_LEVELS = 12;
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;
const char *vertexShaderSource = "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
    "}\0";
const char *fragmentShaderSource = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "void main()\n"
    "{\n"
    "   FragColor = vec4(1.0f, 1.0f, 1.0f, 1.0f);\n"
    "}\n\0";

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void process_input(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE))
        glfwSetWindowShouldClose(window, true);
}

std::vector<float> calculate_sierpinski_points(float vertices[]) {
    std::vector<float> sierpinski_points;
    float alpha = rand() / (float)RAND_MAX;
    float beta = rand() / (float)RAND_MAX;
    float gamma = rand() / (float)RAND_MAX;

    float total = alpha + beta + gamma;
    alpha /= total;
    beta /= total;
    gamma /= total;
    std::cout << alpha << " " << beta << " " << gamma << std::endl;
    float p[3] = {
        alpha * vertices[0] + beta * vertices[3] + gamma * vertices[6],
        alpha * vertices[1] + beta * vertices[4] + gamma * vertices[7],
        alpha * vertices[2] + beta * vertices[5] + gamma * vertices[8]
    };

    int max_iterations = MAX_ITERATIONS;
    while (max_iterations--) {
        int pos_vertex = rand() % 3;
        float *vertex = &vertices[pos_vertex*3];
        float q[3] = {(p[0] + vertex[0]) / 2, (p[1] + vertex[1]) / 2, (p[2] + vertex[2]) / 2};
        for (int i = 0; i < 3; i++) {
            sierpinski_points.push_back(q[i]);
        }
        p[0] = q[0];
        p[1] = q[1];
        p[2] = q[2];
    }

    return sierpinski_points;
}

void draw_aprox_sierpinski(unsigned int& shaderProgram, GLFWwindow* window) {
    std::vector<float> vertices = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f
    };

    auto sierpinski_points = calculate_sierpinski_points(vertices.data());
    vertices.insert(vertices.end(), sierpinski_points.begin(), sierpinski_points.end());

    unsigned int VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * vertices.size(), vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
    glPointSize(5.0f);
    while (!glfwWindowShouldClose(window)) {
        process_input(window);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);
        glDrawArrays(GL_POINTS, 0, vertices.size());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

}


void get_sierpinski_triangle(std::vector<float>& vertices, const std::vector<float> current_triangle, int level = 0) {
    if (level > MAX_LEVELS) return;

    std::vector<float> middle_points;
    for (int i = 0; i < 3; i++) {
        int ni = (i+1) % 3;
        float x = (current_triangle[i*3] + current_triangle[ni*3]) / 2;
        float y = (current_triangle[i*3 + 1] + current_triangle[ni*3 + 1])/2;
        float z = (current_triangle[i*3 + 2] + current_triangle[ni*3 + 2])/2;

        middle_points.push_back(x);
        middle_points.push_back(y);
        middle_points.push_back(z);

        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(z);
    }

    get_sierpinski_triangle(vertices, {
        current_triangle[0], current_triangle[1], current_triangle[2],
        middle_points[0], middle_points[1], middle_points[2],
        middle_points[6], middle_points[7], middle_points[8],
    }, level + 1);
    get_sierpinski_triangle(vertices, {
        middle_points[0], middle_points[1], middle_points[2],
        current_triangle[3], current_triangle[4], current_triangle[5],
        middle_points[3], middle_points[4], middle_points[5],
    }, level + 1);
    get_sierpinski_triangle(vertices, {
        middle_points[3], middle_points[4], middle_points[5],
        current_triangle[6], current_triangle[7], current_triangle[8],
        middle_points[6], middle_points[7], middle_points[8],
    }, level + 1);
}

int draw_sierpinski(unsigned int& shaderProgram, GLFWwindow* window) {
    std::vector<float> vertices = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f
    };

    get_sierpinski_triangle(vertices, vertices);

    unsigned int VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * vertices.size(), vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    while (!glfwWindowShouldClose(window)) {
        process_input(window);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, vertices.size()/3);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

}

int main() {
    srand(time(__null));
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "LearnOpengl", NULL, NULL);
    if (window == NULL) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glEnable(GL_PROGRAM_POINT_SIZE);


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

    glUseProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // draw_aprox_sierpinski(shaderProgram, window);
    draw_sierpinski(shaderProgram, window);

    glfwTerminate();

    return 0;
}
