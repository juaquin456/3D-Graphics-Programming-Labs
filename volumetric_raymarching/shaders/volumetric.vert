#version 450 core

// Fullscreen triangle generation (no VAO buffer allocation needed)
out vec2 v_TexCoord;

void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    v_TexCoord = vec2(x, y) * 0.5 + 0.5;
    gl_Position = vec4(x, y, 0.0, 1.0);
}
