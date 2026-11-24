#version 450 core

in vec3 v_FragPos;
in vec3 v_Normal;
in vec2 v_TexCoords;
in vec4 v_FragPosLightSpace;

out vec4 FragColor;

// Scene configuration
uniform int  u_SceneMode;        // 0: Campfire, 1: Cloud, 2: God Rays
uniform bool u_IsFloor;          // True when rendering the ground plane

// Lighting uniforms
uniform vec3  u_LightPos;
uniform vec3  u_LightColor;
uniform vec3  u_SpotlightDir;
uniform float u_SpotlightCutoff; // cos(cutoff angle)
uniform vec3  u_CameraPos;
uniform vec3  u_ObjectColor;
uniform sampler2D u_ShadowMapTex;

// Point light for campfire flame glow on the floor
uniform vec3  u_FireLightPos;
uniform vec3  u_FireLightColor;
uniform float u_FireLightIntensity;

// Procedural grid computation with analytic anti-aliasing
float computeGrid(vec2 pos, float cellSize, float lineWidth) {
    vec2 coord = pos / cellSize;
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
    float line = min(grid.x, grid.y);
    return 1.0 - min(line / lineWidth, 1.0);
}

float calculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);
    float currentDepth = projCoords.z;

    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(u_ShadowMapTex, 0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(u_ShadowMapTex, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main() {
    vec3 N = normalize(v_Normal);
    vec3 V = normalize(u_CameraPos - v_FragPos);
    vec3 baseColor = u_ObjectColor;

    // Procedural stone/dark soil with subtle metric grid for floor
    if (u_IsFloor) {
        // Dark stone/earth base
        vec3 darkStone = vec3(0.045, 0.048, 0.055);
        vec3 stoneVariation = vec3(0.065, 0.068, 0.075);

        // Subtle large cell texture
        float cellNoise = sin(v_FragPos.x * 3.0) * sin(v_FragPos.z * 3.0);
        baseColor = mix(darkStone, stoneVariation, cellNoise * 0.5 + 0.5);

        // Subtle 3D-engine style coordinate grid (0.5m minor, 2.0m major)
        float minorGrid = computeGrid(v_FragPos.xz, 0.5, 1.2);
        float majorGrid = computeGrid(v_FragPos.xz, 2.0, 1.8);

        // Fade grid smoothly with distance to horizon to prevent aliasing
        float distToCam = length(v_FragPos - u_CameraPos);
        float gridFade = clamp(1.0 - distToCam / 9.0, 0.0, 1.0);

        vec3 gridColor = vec3(0.12, 0.14, 0.18);
        vec3 majorGridColor = vec3(0.18, 0.22, 0.28);

        baseColor = mix(baseColor, gridColor, minorGrid * 0.35 * gridFade);
        baseColor = mix(baseColor, majorGridColor, majorGrid * 0.65 * gridFade);
    }

    vec3 totalLighting = vec3(0.0);

    // 1. Primary Directional / Spotlight
    vec3 L = normalize(u_LightPos - v_FragPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), u_IsFloor ? 24.0 : 64.0);
    vec3 specular = spec * u_LightColor * (u_IsFloor ? 0.2 : 0.65);

    float distPrimary = length(u_LightPos - v_FragPos);
    float attenPrimary = 1.0 / (1.0 + 0.1 * distPrimary + 0.02 * distPrimary * distPrimary);

    if (u_SceneMode == 2) {
        // Spotlight mode for God Rays Scene
        vec3 lightToFrag = normalize(v_FragPos - u_LightPos);
        float cosAngle = dot(lightToFrag, normalize(u_SpotlightDir));
        float coneFactor = smoothstep(u_SpotlightCutoff - 0.02, u_SpotlightCutoff + 0.08, cosAngle);
        float shadow = calculateShadow(v_FragPosLightSpace, N, L);
        float spotAtten = 1.0 / (1.0 + 0.04 * distPrimary * distPrimary);

        vec3 skyAmbient = mix(vec3(0.02, 0.025, 0.04), vec3(0.06, 0.08, 0.14), N.y * 0.5 + 0.5) * baseColor;
        totalLighting = skyAmbient + (1.0 - shadow) * coneFactor * spotAtten * 2.2 * (diff * u_LightColor * baseColor + specular);
    } else {
        // Campfire / Cloud Scene primary lighting
        vec3 ambient = 0.025 * baseColor;
        totalLighting = ambient + attenPrimary * (diff * u_LightColor * baseColor + specular * 0.3);
    }

    // 2. Dynamic Campfire Point Light (Warm flame glow on the floor)
    if (u_SceneMode == 0 && u_FireLightIntensity > 0.0) {
        vec3 L_fire = u_FireLightPos - v_FragPos;
        float distFire = length(L_fire);
        L_fire = normalize(L_fire);

        float diffFire = max(dot(N, L_fire), 0.0);
        float attenFire = 1.0 / (1.0 + 1.8 * distFire + 3.2 * distFire * distFire);

        vec3 H_fire = normalize(L_fire + V);
        float specFire = pow(max(dot(N, H_fire), 0.0), 16.0);

        vec3 fireContrib = (diffFire * baseColor + specFire * 0.4) * u_FireLightColor * (attenFire * u_FireLightIntensity);
        totalLighting += fireContrib;
    }

    FragColor = vec4(totalLighting, 1.0);
}
