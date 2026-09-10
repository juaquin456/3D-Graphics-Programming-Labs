#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
out vec3 FragPos;
out vec4 Color;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    gl_Position = uProjection * uView * worldPos;
    Color = vec4(0.88, 0.86, 0.82, 1);
}