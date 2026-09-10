#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 2) in vec3 aNormal;

out vec3 LightingColor;

struct Material {
    vec3 Ka;
    vec3 Kd;
    vec3 Ks;
    float shininess;
};

struct Light {
    vec3 position;
    vec3 color;
};

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

uniform Material material;
uniform Light light;
uniform vec3 viewPos;

void main() {
    vec3 FragPos = vec3(model * vec4(aPos, 1.0));
    vec3 Normal = mat3(transpose(inverse(model))) * aNormal;

    vec3 ambient = light.color * material.Ka;

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(light.position - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = light.color * (diff * material.Kd);

    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3 specular = light.color * (spec * material.Ks);

    LightingColor = ambient + diffuse + specular;

    gl_Position = projection * view * vec4(FragPos, 1.0);
}