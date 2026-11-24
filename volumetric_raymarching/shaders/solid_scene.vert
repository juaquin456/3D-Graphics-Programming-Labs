#version 450 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

uniform mat4 u_Model;
uniform mat4 u_View;
uniform mat4 u_Projection;
uniform mat4 u_LightSpaceMatrix;

out vec3 v_FragPos;
out vec3 v_Normal;
out vec2 v_TexCoords;
out vec4 v_FragPosLightSpace;

void main() {
    vec4 worldPos = u_Model * vec4(aPos, 1.0);
    v_FragPos = worldPos.xyz;
    v_Normal = mat3(transpose(inverse(u_Model))) * aNormal;
    v_TexCoords = aTexCoords;
    v_FragPosLightSpace = u_LightSpaceMatrix * worldPos;

    gl_Position = u_Projection * u_View * worldPos;
}
