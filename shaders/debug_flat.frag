#version 330 core
out vec4 FragColor;

uniform vec3 u_FlatColor = vec3(0.75, 0.75, 0.75);

void main() {
    FragColor = vec4(u_FlatColor, 1.0);
}
