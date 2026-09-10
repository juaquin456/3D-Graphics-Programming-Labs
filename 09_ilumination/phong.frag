#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;

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

uniform Material material;
uniform Light light;
uniform vec3 viewPos;

void main() {
   vec3 ambient = light.color * material.Ka;

   vec3 norm = normalize(Normal);
   vec3 lightDir = normalize(light.position - FragPos);
   float diff = max(dot(norm, lightDir), 0.0);
   vec3 diffuse = light.color * (diff * material.Kd);

   vec3 viewDir = normalize(viewPos - FragPos);
   // vec3 halfwayDir = normalize(lightDir + viewDir);
   // float spec = pow(max(dot(norm, halfwayDir), 0.0), material.shininess);
   // vec3 specular = light.color * (spec * material.Ks);
   vec3 reflectDir = reflect(-lightDir, norm);
   float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
   vec3 specular = light.color * (spec * material.Ks);

   vec3 result = ambient + diffuse + specular;
   FragColor = vec4(result, 1.0);
}