#version 450 core

in vec2 v_TexCoord;
out vec4 FragColor;

uniform sampler2D u_SceneColorTex;      // Full-resolution solid scene
uniform sampler2D u_VolumetricColorTex; // Full-res or half-res volumetric buffer
uniform int       u_DebugMode;          // 0: Normal, 1: Step Heatmap, 2: Density Only
uniform int       u_SceneMode;          // 0: Campfire, 1: Cloud, 2: God Rays
uniform float     u_Exposure;           // Exposure multiplier (default 1.0)

// Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
vec3 ACESFilm(vec3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Atmospheric sky background gradient for unoccluded background pixels
vec3 getAtmosphericBackground(vec2 uv) {
    if (u_SceneMode == 0) {
        // Nocturnal / twilight gradient: slightly warmer near horizon, deep night aloft
        vec3 horizonSky = vec3(0.025, 0.035, 0.055);
        vec3 zenithSky  = vec3(0.008, 0.012, 0.022);
        return mix(horizonSky, zenithSky, smoothstep(0.0, 0.9, uv.y));
    } else if (u_SceneMode == 1) {
        // Deep cosmic space background with subtle procedural starfield
        vec3 deepSpace = mix(vec3(0.006, 0.008, 0.018), vec3(0.016, 0.012, 0.032), smoothstep(0.0, 1.0, uv.y));
        vec2 starCell = floor(uv * vec2(320.0, 180.0));
        float h = fract(sin(dot(starCell, vec2(127.1, 311.7))) * 43758.5453123);
        if (h > 0.994) {
            float twinkle = (h - 0.994) / 0.006;
            vec2 cellFract = fract(uv * vec2(320.0, 180.0)) - 0.5;
            float starGlow = smoothstep(0.35, 0.0, length(cellFract)) * twinkle;
            deepSpace += vec3(0.75, 0.82, 1.0) * starGlow * 0.85;
        }
        return deepSpace;
    } else {
        // Courtyard nocturnal penumbra
        vec3 horizonSky = vec3(0.015, 0.018, 0.025);
        vec3 zenithSky  = vec3(0.006, 0.008, 0.014);
        return mix(horizonSky, zenithSky, uv.y);
    }
}

void main() {
    vec4 volSample = texture(u_VolumetricColorTex, v_TexCoord);
    vec4 solidSample = texture(u_SceneColorTex, v_TexCoord);

    // In Heatmap or Density Only debug mode, display diagnostic buffer directly
    if (u_DebugMode != 0) {
        FragColor = volSample;
        return;
    }

    // Determine background color: solid scene geometry or atmospheric sky gradient
    // solidSample.a == 0.0 means no solid geometry was drawn on that pixel
    vec3 backgroundColor = (solidSample.a > 0.0) ? solidSample.rgb : getAtmosphericBackground(v_TexCoord);

    // Alpha blend volumetric result over solid scene / atmospheric background
    // Front-to-back: C = C_vol + C_background * (1.0 - A_vol)
    vec3 blended = volSample.rgb + backgroundColor * (1.0 - volSample.a);

    // Apply exposure scale
    float expFactor = (u_Exposure > 0.01) ? u_Exposure : 1.0;
    vec3 exposed = blended * expFactor;

    // ACES Tone mapping for HDR range preservation
    vec3 mapped = ACESFilm(exposed);

    // Gamma correction to sRGB
    mapped = pow(mapped, vec3(1.0 / 2.2));

    FragColor = vec4(mapped, 1.0);
}
