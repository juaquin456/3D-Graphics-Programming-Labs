#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec3 aTangent;

out vec3 FragPos;
out vec3 Normal;
out vec4 FragPosLightSpace;
out vec2 TexCoord;
out mat3 TBN;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;

void main() {
  FragPos = vec3(model * vec4(aPos, 1.0));
  FragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
  TexCoord = aTexCoord;
  
  mat3 normalMatrix = mat3(transpose(inverse(model)));

  vec3 N = normalize(normalMatrix * aNormal);
  vec3 T = normalize(normalMatrix * aTangent);

  T = normalize(T - dot(T, N) * N);
  vec3 B = cross(N, T);
  
  TBN = mat3(T, B, N);
  Normal = N;
  gl_Position = projection * view * vec4(FragPos, 1.0);
}
