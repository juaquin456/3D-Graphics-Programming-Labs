#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in vec4 FragPosLightSpace;
in mat3 TBN;

struct MaterialTex {
    sampler2D diffuseMap;
    sampler2D specularMap;
    sampler2D normalMap;
    bool hasNormalMap;
    vec3 Ke;
    float shininess;
};

struct Light {
    vec3 position;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

uniform MaterialTex material;
uniform Light light;
uniform vec3 viewPos;
uniform bool useBlinn;

uniform bool useShadows;
uniform sampler2D shadowMap;

float calculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 0.0;

    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);

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
    vec3 norm;
    if (material.hasNormalMap) {
      norm = texture(material.normalMap, TexCoord).rgb;
      norm = norm * 2.0 - 1.0;
      norm = normalize(TBN * norm);
    } else {
      norm = normalize(Normal);
    }

    vec3 kd = texture(material.diffuseMap, TexCoord).rgb;
    vec3 ks = texture(material.specularMap, TexCoord).rgb;

    vec3 ambient = light.ambient * kd;

    vec3 lightDir = normalize(light.position - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = light.diffuse * (diff * kd);

    vec3 viewDir = normalize(viewPos - FragPos);
    float spec = 0.0;

    if (useBlinn) {
        vec3 halfwayDir = normalize(lightDir + viewDir);
        spec = pow(max(dot(norm, halfwayDir), 0.0), material.shininess * 3.0);
    } else {
        vec3 reflectDir = reflect(-lightDir, norm);
        spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    }
    vec3 specular = light.specular * (spec * ks);

    float shadow = useShadows ? calculateShadow(FragPosLightSpace, norm, lightDir) : 0.0;
    
    vec3 result = material.Ke + ambient + (1.0 - shadow) * (diffuse + specular);
    FragColor = vec4(result, 1.0);
}
