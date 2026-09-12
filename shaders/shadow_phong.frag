#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec4 FragPosLightSpace;

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
uniform sampler2D shadowMap;

float calculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
  vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

  projCoords = projCoords * 0.5 + 0.5;

  if (projCoords.z > 1.0) return 0.0;

  float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);

  float shadow = 0.0;
  vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
  for(int x = -1; x <= 1; ++x) {
    for(int y = -1; y <= 1; ++y) {
      float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
      shadow += (projCoords.z - bias > pcfDepth) ? 1.0 : 0.0;
    }
  }
  shadow /= 9.0;
  return shadow;
}

void main() {
  vec3 ambient = light.color * material.Ka;

  vec3 norm = normalize(Normal);
  vec3 lightDir = normalize(light.position - FragPos);
  float diff = max(dot(norm, lightDir), 0.0);
  vec3 diffuse = light.color * (diff * material.Kd);

  vec3 viewDir = normalize(viewPos - FragPos);
  vec3 reflectDir = reflect(-lightDir, norm);
  float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
  vec3 specular = spec * material.Ks * light.color;

  float shadow = calculateShadow(FragPosLightSpace, norm, lightDir);

  vec3 result = ambient + (1.0 - shadow) * (diffuse + specular);
  FragColor = vec4(result, 1.0);
}
