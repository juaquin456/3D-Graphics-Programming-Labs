#version 450 core

// ============================================================================
// DIDACTIC VOLUMETRIC RAY MARCHING SHADER (CINEMATIC HIGH-FIDELITY)
//
// Modular architecture designed for classroom presentation:
// 1. intersectAABB(): Branchless slab intersection with camera-inside support
// 2. evalBlackbody(): Real-time physical color temperature emission curve
// 3. phaseHenyeyGreenstein(): Anisotropic scattering (Silver Lining effect)
// 4. sampleVolume(): Unified sampling (Campfire VDB, Procedural Cloud, God Rays)
// 5. evalSelfShadow() & evalSpotlightShadow(): Secondary shadow rays
// 6. rayMarch(): Primary accumulation loop with IGN dithering & early exit
// 7. evalHeatmap(): 4-stop thermal diagnostic color visualization
// ============================================================================

in vec2 v_TexCoord;
out vec4 FragColor;

// --- CAMERA & MATRICES ---
uniform mat4  u_InvView;
uniform mat4  u_InvProjection;
uniform vec3  u_CameraPos;
uniform vec2  u_ScreenResolution;
uniform float u_NearPlane;
uniform float u_FarPlane;
uniform float u_Time;

// --- SCENE CONFIGURATION ---
// 0: SmallCampfireVDB, 1: Procedural Cloud (Silver Lining), 2: God Rays & Geometry
uniform int u_SceneMode;

// --- BOUNDING BOX (AABB) ---
uniform vec3 u_BoxMin;
uniform vec3 u_BoxMax;

// --- RAY MARCHING PARAMETERS ---
uniform int   u_MaxSteps;             // e.g. 64 to 256 (default 128)
uniform float u_StepSize;             // world-space distance per step
uniform bool  u_EnableJitter;         // Ray dithering with Interleaved Gradient Noise
uniform bool  u_EnableSelfShadow;     // Secondary shadow rays
uniform int   u_ShadowSteps;          // Secondary shadow steps (e.g. 4 to 6)
uniform int   u_DebugMode;            // 0: Normal, 1: Step Heatmap, 2: Density Only

// --- OPTICAL & PHYSICAL PROPERTIES ---
uniform float u_SigmaA;               // Absorption coefficient (Beer-Lambert law)
uniform float u_SigmaS;               // Scattering coefficient
uniform float u_HenyeyG;              // Anisotropy parameter g in [-0.8, 0.9]
uniform float u_EmissionIntensity;    // Blackbody thermal radiation multiplier
uniform float u_DensityMultiplier;    // Global density scale

// --- LIGHTING ---
uniform vec3  u_LightPos;
uniform vec3  u_LightColor;
uniform float u_LightIntensity;
uniform mat4  u_LightSpaceMatrix;     // For spotlight shadow map lookup (Scene 3)
uniform vec3  u_SpotlightDir;
uniform float u_SpotlightCutoff;      // cos(cutoff angle)

// --- TEXTURES & VOLUMETRIC DATA ---
uniform sampler3D u_VolumeTexCurrent; // Campfire frame N (GL_RG8)
uniform sampler3D u_VolumeTexNext;    // Campfire frame N+1 (GL_RG8)
uniform float     u_FrameBlend;       // Temporal interpolation [0.0, 1.0]
uniform sampler3D u_NoiseTex3D;       // Precomputed 64^3 tileable Perlin-Worley noise
uniform sampler2D u_SceneDepthTex;    // Solid scene rasterized depth buffer
uniform sampler2D u_ShadowMapTex;     // Spotlight 2D shadow map

// Physical Constants
const float PI = 3.14159265358979323846;
const float INV_4PI = 0.07957747154594766788;

// ============================================================================
// 1. RAY-AABB INTERSECTION (SLAB METHOD - BRANCHLESS)
// ============================================================================
// Calculates entry (tNear) and exit (tFar) distances along ray through the box.
// Includes division-by-zero protection and handles camera inside box (tNear = 0).
bool intersectAABB(vec3 rayOrigin, vec3 rayDir, vec3 boxMin, vec3 boxMax,
                  out float tNear, out float tFar)
{
    vec3 safeDir = rayDir + sign(rayDir) * 1e-7;
    vec3 invDir = 1.0 / safeDir;

    vec3 tBot = invDir * (boxMin - rayOrigin);
    vec3 tTop = invDir * (boxMax - rayOrigin);

    vec3 tMin = min(tBot, tTop);
    vec3 tMax = max(tBot, tTop);

    float enter = max(max(tMin.x, tMin.y), tMin.z);
    float exit  = min(min(tMax.x, tMax.y), tMax.z);

    if (enter > exit || exit < 0.0) {
        tNear = 0.0;
        tFar  = 0.0;
        return false;
    }

    tNear = max(enter, 0.0); // Inside box support
    tFar  = exit;
    return true;
}

// ============================================================================
// 2. BLACKBODY RADIATION (PHYSICALLY-BASED HDR EMISSION)
// ============================================================================
// Maps temperature to Planckian incandescent color curve:
// Extinguished -> Crimson Red -> Pure Flame Orange -> Golden Yellow -> Incandescent White
vec3 evalBlackbody(float temp) {
    if (temp <= 0.005) return vec3(0.0);

    // High-Dynamic-Range chromaticity keypoints
    vec3 c0 = vec3(0.0, 0.0, 0.0);           // Extinguished
    vec3 c1 = vec3(0.85, 0.06, 0.01);        // Deep crimson glow (smoldering base)
    vec3 c2 = vec3(1.35, 0.44, 0.02);        // Saturated flame orange
    vec3 c3 = vec3(2.40, 1.65, 0.28);        // Intense golden yellow core
    vec3 c4 = vec3(4.80, 4.50, 4.00);        // Incandescent white-hot center

    vec3 col;
    if (temp < 0.22) {
        col = mix(c0, c1, temp / 0.22);
    } else if (temp < 0.48) {
        col = mix(c1, c2, (temp - 0.22) / 0.26);
    } else if (temp < 0.72) {
        col = mix(c2, c3, (temp - 0.48) / 0.24);
    } else {
        col = mix(c3, c4, (temp - 0.72) / 0.28);
    }

    // Stefan-Boltzmann $T^4$ radiance growth approximation
    float radianceBoost = pow(temp, 2.5) * 2.2;
    return col * (1.0 + radianceBoost);
}

// ============================================================================
// 3. HENYEY-GREENSTEIN PHASE FUNCTION
// ============================================================================
// Governs directional light scattering inside aerosols, droplets, and smoke:
//   P(cosTheta, g) = (1 / 4*PI) * (1 - g^2) / (1 + g^2 - 2*g*cosTheta)^(3/2)
// Forward scattering (g > 0) creates the brilliant Silver Lining when facing the light.
float phaseHenyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 0.0001);
    return INV_4PI * ((1.0 - g2) / (denom * sqrt(denom)));
}

// ============================================================================
// 4. INTERLEAVED GRADIENT NOISE (IGN) DITHERING
// ============================================================================
// Jorge Jimenez (Call of Duty, Next-Gen Post Processing).
// Completely eliminates step-banding artifacts with clean, high-frequency spatial noise.
float interleavedGradientNoise(vec2 screenPos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

// 4-Octave 3D Worley/Perlin-Worley FBM for turbulent filaments and clouds
float sampleWorleyFBM(vec3 pos) {
    vec3 p = pos * 0.45 + vec3(u_Time * 0.010, 0.0, u_Time * 0.005);
    float f = 0.0;
    float amp = 0.50;

    for (int oct = 0; oct < 4; ++oct) {
        vec4 n = texture(u_NoiseTex3D, p);
        float val = (oct == 0) ? n.r : mix(n.r, n.g, 0.65);
        f += amp * val;
        p = p * 2.03 + vec3(0.19, 0.37, 0.53);
        amp *= 0.50;
    }
    return f;
}

// Cosmic Nebula / Accretion Disk with Differential Swirl + 3D Domain Warping
void sampleNebula(vec3 worldPos, out float density, out float emitMask, out vec3 emitColor) {
    density = 0.0;
    emitMask = 0.0;
    emitColor = vec3(0.0);

    vec3 p = worldPos - vec3(0.0, 1.15, 0.0);
    float rXZ = length(p.xz);
    float r3D = length(p);
    if (r3D > 1.75) return;

    // 1. Differential Keplerian-style spiral swirl around the core
    float swirlAngle = 2.1 / (rXZ + 0.35) + u_Time * 0.05;
    float cs = cos(swirlAngle);
    float sn = sin(swirlAngle);
    vec3 sp = vec3(p.x * cs - p.z * sn, p.y, p.x * sn + p.z * cs);

    // 2. 3D Domain Warping: twists space into organic pillars & cavities
    vec3 warp = (texture(u_NoiseTex3D, sp * 0.32 + vec3(u_Time * 0.006)).rgb - 0.5) * 0.88;
    vec3 wp = sp + warp;

    // 3. Macro envelope: flared accretion disk + subtle bipolar halo
    float diskProfile = exp(-abs(wp.y) * 3.2) * smoothstep(1.65, 0.15, length(wp.xz));
    float haloProfile = exp(-r3D * 2.1) * 0.55;
    float macroShape = max(diskProfile, haloProfile);

    if (macroShape < 0.02) return;

    // 4. Multi-octave turbulent filaments + fine Worley dust lanes
    float fbm = sampleWorleyFBM(wp * 1.15);
    float fineWorley = texture(u_NoiseTex3D, wp * 1.85).g;

    // Carve sharp dark filaments and cavities
    float rawDensity = macroShape * smoothstep(0.24, 0.78, fbm) * 2.4;
    // Dense cold dust lanes (high absorption, zero emission)
    float dustLane = smoothstep(0.45, 0.82, fineWorley) * smoothstep(0.25, 0.85, r3D);

    density = clamp(rawDensity + dustLane * macroShape * 1.35, 0.0, 2.5) * u_DensityMultiplier;
    if (density <= 0.001) return;

    // 5. Multi-spectral emission:
    // Hot stellar core -> golden-white; inner ionized arms -> magenta/cyan; outer dust -> 0
    float coreGlow = exp(-r3D * 6.5);
    float armIonization = smoothstep(0.45, 0.10, dustLane) * smoothstep(1.35, 0.25, r3D);

    vec3 coreCol = vec3(5.0, 3.4, 1.4);
    vec3 hAlpha  = vec3(1.9, 0.28, 0.75); // Ionized hydrogen (magenta-rose)
    vec3 oThree  = vec3(0.18, 0.85, 2.1); // Doubly-ionized oxygen (electric cyan)

    float colorMix = clamp(texture(u_NoiseTex3D, wp * 0.55).r * 1.4 - 0.2, 0.0, 1.0);
    vec3 gasCol = mix(hAlpha, oThree, colorMix);

    emitColor = coreCol * coreGlow + gasCol * armIonization * (0.45 + 0.75 * fbm);
    emitMask  = clamp(coreGlow + armIonization * 0.85, 0.0, 1.0);
}

// ============================================================================
// 5. UNIFIED VOLUME SAMPLING (ALL 3 SCENES)
// ============================================================================
void sampleVolume(vec3 pos, out float density, out float temp) {
    density = 0.0;
    temp = 0.0;

    if (u_SceneMode == 0) {
        // --- ESCENA 1: FOGATA VOLUMETRICA (VDB .RAW) ---
        vec3 uvw = (pos - u_BoxMin) / (u_BoxMax - u_BoxMin);

        // Hardware CLAMP_TO_BORDER returns vec4(0.0) outside [0, 1]
        vec2 sampleA = texture(u_VolumeTexCurrent, uvw).rg;
        vec2 sampleB = texture(u_VolumeTexNext, uvw).rg;
        vec2 interpolated = mix(sampleA, sampleB, u_FrameBlend);

        density = interpolated.r * u_DensityMultiplier;
        temp    = interpolated.g; // Channel G: normalized temperature [0, 1]
    }
    else if (u_SceneMode == 1) {
        // --- ESCENA 2: NEBULOSA COSMICA / DISCO DE ACRECION VOLUMETRICO ---
        vec3 dummyEmit;
        sampleNebula(pos, density, temp, dummyEmit);
    }
    else if (u_SceneMode == 2) {
        // --- ESCENA 3: NIEBLA VOLUMETRICA HETEROGENEA PARA GOD RAYS ---
        if (pos.y >= 0.0 && pos.y <= 3.9 &&
            abs(pos.x) <= 3.9 && abs(pos.z) <= 3.9) {
            float heightFalloff = exp(-pos.y * 0.25);
            // 2 octavas de ruido 3D tileable animado lentamente (bruma en suspension)
            vec3 nCoord = pos * 0.22 + vec3(u_Time * 0.012, -u_Time * 0.005, u_Time * 0.008);
            float n1 = texture(u_NoiseTex3D, nCoord).r;
            float n2 = texture(u_NoiseTex3D, nCoord * 2.1 + vec3(0.37)).g;
            float mistNoise = mix(0.45, 1.35, clamp(n1 * 0.65 + n2 * 0.35, 0.0, 1.0));
            density = 0.38 * heightFalloff * mistNoise * u_DensityMultiplier;
            temp = 0.0;
        }
    }
}

// ============================================================================
// 6. SHADOW & LIGHT ACCUMULATION
// ============================================================================

// Evaluates spotlight shadow map for Scene 3 (God Rays)
float evalSpotlightShadow(vec3 worldPos) {
    vec4 fragPosLightSpace = u_LightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0) {
        return 0.0;
    }

    vec3 lightToPos = normalize(worldPos - u_LightPos);
    float cosAngle = dot(lightToPos, normalize(u_SpotlightDir));
    if (cosAngle < u_SpotlightCutoff - 0.02) {
        return 0.0;
    }
    float coneFactor = smoothstep(u_SpotlightCutoff - 0.02, u_SpotlightCutoff + 0.08, cosAngle);

    float closestDepth = texture(u_ShadowMapTex, projCoords.xy).r;
    float currentDepth = projCoords.z;
    float bias = 0.0025;
    float shadow = (currentDepth - bias > closestDepth) ? 0.0 : 1.0;

    return shadow * coneFactor;
}

// Evaluates secondary shadow ray through smoke volume towards point light (Self-Shadowing)
float evalSelfShadow(vec3 pos, vec3 lightDir, float distToLight) {
    if (!u_EnableSelfShadow) return 1.0;

    int shadowSteps = u_ShadowSteps;
    float maxShadowDist = (u_SceneMode == 1) ? 1.60 : 1.35;
    float stepLen = min(distToLight, maxShadowDist) / float(shadowSteps);
    float shadowOpticalDepth = 0.0;

    for (int i = 1; i <= shadowSteps; ++i) {
        vec3 shadowPos = pos + lightDir * (float(i) * stepLen);
        float sDens, sTemp;
        sampleVolume(shadowPos, sDens, sTemp);
        shadowOpticalDepth += sDens * stepLen;
    }

    return exp(-shadowOpticalDepth * u_SigmaA * 1.35);
}

// ============================================================================
// 7. STEP COUNT HEATMAP COLOR MAPPING (DIAGNOSTIC MODE)
// ============================================================================
vec3 evalHeatmap(float normalizedSteps) {
    if (normalizedSteps <= 0.0) {
        return vec3(0.04, 0.05, 0.18); // Deep Navy Blue: 0 steps (AABB discard)
    }

    // 4-stop vibrant diagnostic thermal gradient
    vec3 c0 = vec3(0.0, 0.65, 1.0);  // Cyan (low steps / early exit)
    vec3 c1 = vec3(0.1, 0.95, 0.2);  // Bright Green
    vec3 c2 = vec3(1.0, 0.88, 0.0);  // Yellow
    vec3 c3 = vec3(1.0, 0.20, 0.0);  // Vivid Orange-Red
    vec3 c4 = vec3(1.0, 0.0, 0.45);  // Hot Magenta/Red (max steps)

    float t = clamp(normalizedSteps, 0.0, 1.0);
    if (t < 0.25) return mix(c0, c1, t / 0.25);
    if (t < 0.50) return mix(c1, c2, (t - 0.25) / 0.25);
    if (t < 0.75) return mix(c2, c3, (t - 0.50) / 0.25);
    return mix(c3, c4, (t - 0.75) / 0.25);
}

float linearizeDepth(float zBufferVal, vec3 rayDir) {
    if (zBufferVal >= 1.0) return 1000.0;
    float zNdc = zBufferVal * 2.0 - 1.0;
    // Linear depth along camera view direction (-Z in view space)
    float linearZ = (2.0 * u_NearPlane * u_FarPlane) / (u_FarPlane + u_NearPlane - zNdc * (u_FarPlane - u_NearPlane));
    // Reconstruct Euclidean distance along world-space ray
    vec3 camForward = -vec3(u_InvView[2].xyz);
    float cosView = max(dot(rayDir, camForward), 0.0001);
    return linearZ / cosView;
}

// ============================================================================
// 8. PRIMARY RAY MARCHING PIPELINE
// ============================================================================
void main() {
    // 1. Reconstruct world-space ray from screen coordinates
    vec2 ndc = v_TexCoord * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, -1.0, 1.0);
    vec4 viewPos = u_InvProjection * clipPos;
    viewPos /= viewPos.w;
    vec3 rayDir = normalize((u_InvView * vec4(viewPos.xyz, 0.0)).xyz);
    vec3 rayOrigin = u_CameraPos;

    // 2. Intersect ray with volume Axis-Aligned Bounding Box (AABB)
    float tNear, tFar;
    bool hitBox = intersectAABB(rayOrigin, rayDir, u_BoxMin, u_BoxMax, tNear, tFar);

    // 3. Read rasterized solid geometry Z-Buffer to clip ray marching exactly at solid surfaces
    float solidDepthRaw = texture(u_SceneDepthTex, v_TexCoord).r;
    float solidDist = linearizeDepth(solidDepthRaw, rayDir);

    if (!hitBox || solidDist <= tNear) {
        if (u_DebugMode == 1) {
            FragColor = vec4(evalHeatmap(0.0), 1.0);
        } else {
            FragColor = vec4(0.0);
        }
        return;
    }

    tFar = min(tFar, solidDist);
    float rayDistance = max(tFar - tNear, 0.0);
    if (rayDistance <= 0.0001) {
        FragColor = (u_DebugMode == 1) ? vec4(evalHeatmap(0.0), 1.0) : vec4(0.0);
        return;
    }

    // 4. Adaptive Step Sizing (ensures full traversal from tNear to tFar even on large AABBs)
    float stepSize = max(u_StepSize, rayDistance / float(u_MaxSteps));
    int maxSteps = min(int(ceil(rayDistance / stepSize)), u_MaxSteps);

    // 5. Interleaved Gradient Noise (IGN) Ray Jittering (Eliminates Banding)
    float jitter = 0.0;
    if (u_EnableJitter) {
        jitter = interleavedGradientNoise(gl_FragCoord.xy);
    }
    float currentT = tNear + jitter * stepSize;

    // 6. Accumulation Variables (Front-to-Back)
    vec3  accColor = vec3(0.0);
    float accAlpha = 0.0;
    float accDensityOnly = 0.0;
    int   stepCount = 0;

    // Phase evaluation for primary light
    vec3 lightVec = u_LightPos - rayOrigin;
    vec3 lightDir = normalize(lightVec);
    float cosTheta = dot(rayDir, lightDir);
    float phaseVal = phaseHenyeyGreenstein(cosTheta, u_HenyeyG);

    // Internal flame core illumination (illuminates the rising smoke plume from within)
    vec3 internalFirePos = vec3(0.0, 0.22, 0.0);
    vec3 internalFireColor = vec3(1.2, 0.52, 0.08) * 3.8;

    // 7. Ray Marching Loop
    for (int i = 0; i < maxSteps; ++i) {
        if (currentT >= tFar) break;

        stepCount++;
        vec3 currentPos = rayOrigin + rayDir * currentT;

        float density, temp;
        vec3 nebulaEmitColor = vec3(0.0);
        if (u_SceneMode == 1) {
            sampleNebula(currentPos, density, temp, nebulaEmitColor);
        } else {
            sampleVolume(currentPos, density, temp);
        }

        if (density > 0.0001) {
            // BEER-LAMBERT ABSORPTION: Transmittance T = exp(-sigma_a * density * stepSize)
            float stepOpticalDepth = u_SigmaA * density * stepSize;
            float stepTransmittance = exp(-stepOpticalDepth);
            float stepAlpha = 1.0 - stepTransmittance;

            vec3 stepRadiance = vec3(0.0);

            // A) Thermal / Spectral Emission (Campfire Blackbody or Nebula Ionized Gas)
            if (u_SceneMode == 1) {
                stepRadiance += nebulaEmitColor * u_EmissionIntensity;
            } else if (temp > 0.01 && u_EmissionIntensity > 0.0) {
                vec3 emissionColor = evalBlackbody(temp);
                stepRadiance += emissionColor * u_EmissionIntensity;
            }

            // B) External Light In-Scattering
            vec3 toLight = u_LightPos - currentPos;
            float distToLight = length(toLight);
            vec3 L = toLight / max(distToLight, 0.001);

            float lightShadow = 1.0;
            float localPhase = phaseVal;
            float lightAtten = 1.0;

            if (u_SceneMode == 2) {
                lightShadow = evalSpotlightShadow(currentPos);
                // Per-sample phase function for accurate spotlight forward-scattering shafts
                localPhase = phaseHenyeyGreenstein(dot(rayDir, L), u_HenyeyG);
                lightAtten = u_LightIntensity / (1.0 + 0.05 * distToLight * distToLight);
                // Subtle nocturnal ambient mist scattering so shadowed air still reads as dense
                stepRadiance += vec3(0.022, 0.036, 0.068) * (u_SigmaS * density);
            } else if (u_SceneMode == 1) {
                // External orbital light with strong forward-scattering Silver Lining on dust
                lightShadow = evalSelfShadow(currentPos, L, distToLight);
                localPhase = phaseHenyeyGreenstein(dot(rayDir, L), u_HenyeyG);
                lightAtten = u_LightIntensity / (1.0 + 0.08 * distToLight * distToLight);

                // Central Star Radial Illumination + Self-Shadowing (3D internal crepuscular rays!)
                vec3 starPos = vec3(0.0, 1.15, 0.0);
                vec3 toStar = starPos - currentPos;
                float distStar = length(toStar);
                if (distStar > 0.05) {
                    vec3 L_star = toStar / distStar;
                    float starShadow = evalSelfShadow(currentPos, L_star, distStar);
                    float starPhase = phaseHenyeyGreenstein(dot(rayDir, L_star), 0.55);
                    float starAtten = 5.5 / (0.25 + distStar * distStar * 1.4);
                    stepRadiance += vec3(1.0, 0.78, 0.45) * starAtten * starShadow * starPhase * (u_SigmaS * density);
                }
            } else {
                lightShadow = evalSelfShadow(currentPos, L, distToLight);
                lightAtten = u_LightIntensity / max(distToLight * distToLight * 0.35 + 1.0, 1.0);
            }

            vec3 inScattered = u_LightColor * lightAtten * lightShadow * localPhase * (u_SigmaS * density);
            stepRadiance += inScattered;

            // C) Internal Fire-to-Smoke Glow (Campfire Scene)
            if (u_SceneMode == 0 && currentPos.y > 0.18) {
                vec3 toFire = internalFirePos - currentPos;
                float distFire = length(toFire);
                float fireAtten = 1.0 / (distFire * distFire * 4.5 + 1.0);
                stepRadiance += internalFireColor * fireAtten * (density * 1.8);
            }

            // FRONT-TO-BACK COMPOSITING
            accColor += (1.0 - accAlpha) * stepRadiance * stepAlpha;
            accAlpha += (1.0 - accAlpha) * stepAlpha;

            accDensityOnly += density * stepSize;

            // EARLY RAY TERMINATION: Break when pixel is opaque (>= 98%)
            if (accAlpha >= 0.98) {
                accAlpha = 1.0;
                break;
            }
        }

        currentT += stepSize;
    }

    // 8. Output selection based on active Debug Visualization Mode
    if (u_DebugMode == 1) {
        // Step Count Heatmap (Visualizes Early Ray Termination & Discard)
        float normalizedSteps = float(stepCount) / float(u_MaxSteps);
        FragColor = vec4(evalHeatmap(normalizedSteps), 1.0);
    }
    else if (u_DebugMode == 2) {
        // Density Only (Monochrome raw density projection)
        float d = clamp(accDensityOnly * 0.9, 0.0, 1.0);
        FragColor = vec4(vec3(d), d);
    }
    else {
        // Cinematic Volumetric Beauty Pass
        FragColor = vec4(accColor, accAlpha);
    }
}
