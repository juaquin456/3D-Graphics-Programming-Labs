#include "DiagnosticUI.h"
#include <iostream>

namespace vrm {

DiagnosticUI::DiagnosticUI() {}

void DiagnosticUI::render(SceneSettings& settings, Camera& camera, float fps, float frameTimeMs) {
    ImGui::SetNextWindowPos(ImVec2(15, 15), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 710), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_FirstUseEver);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None;
    if (ImGui::Begin("Volumetric Ray Marching - Control Didactico", nullptr, windowFlags)) {
        renderPerformanceMetrics(fps, frameTimeMs, settings);
        ImGui::Separator();

        renderSceneSelector(settings, camera);
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Calidad de Muestreo", ImGuiTreeNodeFlags_DefaultOpen)) {
            renderRayMarchingOptimizations(settings);
        }

        if (ImGui::CollapsingHeader("Fisica de Luz & Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            renderOpticalParameters(settings);
        }

        if (ImGui::CollapsingHeader("Iluminacion & Posicion de Luz", ImGuiTreeNodeFlags_DefaultOpen)) {
            renderLightControls(settings);
        }

        if (settings.type == SceneType::CampfireVDB) {
            if (ImGui::CollapsingHeader("Animacion Campfire VDB", ImGuiTreeNodeFlags_DefaultOpen)) {
                renderPlaybackControls(settings);
            }
        }

        if (ImGui::CollapsingHeader("Diagnostico / Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
            renderDebugModes(settings);
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Camara & Navegacion")) {
            ImGui::BulletText("Click Izquierdo + Arrastrar: Orbitar");
            ImGui::BulletText("Rueda del Raton: Zoom In / Out");
            ImGui::BulletText("Click Derecho + Arrastrar: Pan");
            if (ImGui::Button("Resetear Camara de Escena")) {
                if (settings.type == SceneType::CampfireVDB) {
                    camera.setTarget(glm::vec3(0.0f, 0.45f, 0.0f));
                    camera.setDistance(1.55f);
                    camera.setAngles(45.0f, 18.0f);
                } else if (settings.type == SceneType::ProceduralCloud) {
                    camera.setTarget(glm::vec3(0.0f, 1.15f, 0.0f));
                    camera.setDistance(2.85f);
                    camera.setAngles(20.0f, 26.0f);
                } else {
                    camera.setTarget(glm::vec3(0.0f, 1.1f, 0.0f));
                    camera.setDistance(5.4f);
                    camera.setAngles(38.0f, 16.0f);
                }
            }
        }
    }
    ImGui::End();
}

void DiagnosticUI::renderPerformanceMetrics(float fps, float frameTimeMs, const SceneSettings& settings) {
    ImVec4 fpsColor = (fps >= 55.0f) ? ImVec4(0.2f, 0.9f, 0.2f, 1.0f) :
                      (fps >= 30.0f) ? ImVec4(1.0f, 0.8f, 0.1f, 1.0f) :
                                       ImVec4(0.9f, 0.2f, 0.2f, 1.0f);

    ImGui::TextColored(fpsColor, "Rendimiento: %.1f FPS", fps);
    ImGui::SameLine();
    ImGui::Text("(%.2f ms)", frameTimeMs);

    if (settings.halfRes) {
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Resolucion: 0.5x Downsampling (640x360)");
    } else {
        ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.5f, 1.0f), "Resolucion: 1.0x Nativa Completa (1280x720)");
    }
}

void DiagnosticUI::renderSceneSelector(SceneSettings& settings, Camera& camera) {
    const char* sceneNames[] = {
        "1: Fogata Volumetrica (SmallCampfireVDB)",
        "2: Nebulosa Cosmica (Volumen 3D)",
        "3: God Rays + Geometria Rasterizada"
    };

    int currentIdx = static_cast<int>(settings.type);
    ImGui::Text("Escena:");
    if (ImGui::Combo("##SceneCombo", &currentIdx, sceneNames, 3)) {
        settings.type = static_cast<SceneType>(currentIdx);

        if (settings.type == SceneType::CampfireVDB) {
            settings.boxMin = glm::vec3(-0.35f, 0.0f, -0.35f);
            settings.boxMax = glm::vec3( 0.35f, 1.0f,  0.35f);
            settings.sigmaA = 6.5f;
            settings.sigmaS = 4.5f;
            settings.emissionIntensity = 4.2f;
            settings.lightPos = glm::vec3(0.9f, 1.6f, 0.9f);
            settings.lightColor = glm::vec3(1.0f, 0.94f, 0.88f);
            settings.lightIntensity = 2.8f;
            camera.setTarget(glm::vec3(0.0f, 0.45f, 0.0f));
            camera.setDistance(1.55f);
            camera.setAngles(45.0f, 18.0f);
        } else if (settings.type == SceneType::ProceduralCloud) {
            settings.boxMin = glm::vec3(-1.8f, 0.1f, -1.8f);
            settings.boxMax = glm::vec3( 1.8f, 2.2f,  1.8f);
            settings.sigmaA = 3.8f;
            settings.sigmaS = 5.2f;
            settings.henyeyG = 0.72f;
            settings.emissionIntensity = 2.6f;
            settings.densityMultiplier = 1.45f;
            settings.lightPos = glm::vec3(0.0f, 1.55f, -3.0f);
            settings.lightColor = glm::vec3(0.85f, 0.92f, 1.0f);
            settings.lightIntensity = 4.5f;
            camera.setTarget(glm::vec3(0.0f, 1.15f, 0.0f));
            camera.setDistance(2.85f);
            camera.setAngles(20.0f, 26.0f);
        } else if (settings.type == SceneType::GodRaysGeometry) {
            settings.boxMin = glm::vec3(-3.9f, 0.0f, -3.9f);
            settings.boxMax = glm::vec3( 3.9f, 4.0f,  3.9f);
            settings.sigmaA = 0.55f;
            settings.sigmaS = 3.2f;
            settings.henyeyG = 0.68f;
            settings.emissionIntensity = 0.0f;
            settings.densityMultiplier = 1.25f;
            settings.lightPos = glm::vec3(-2.8f, 3.6f, -2.4f);
            settings.spotlightTarget = glm::vec3(0.4f, 0.4f, 0.4f);
            settings.spotlightCutoff = 38.0f;
            settings.lightColor = glm::vec3(1.0f, 0.88f, 0.70f);
            settings.lightIntensity = 5.5f;
            camera.setTarget(glm::vec3(0.0f, 1.1f, 0.0f));
            camera.setDistance(5.4f);
            camera.setAngles(38.0f, 16.0f);
        }
    }
}

void DiagnosticUI::renderRayMarchingOptimizations(SceneSettings& settings) {
    ImGui::SliderInt("Pasos del Rayo", &settings.maxSteps, 32, 256);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Numero maximo de muestras tomadas a lo largo de cada rayo de camara.");

    ImGui::SliderFloat("Tamanio de Paso (dt)", &settings.stepSize, 0.005f, 0.05f, "%.3f");

    ImGui::Checkbox("Ray Jittering (IGN Dithering)", &settings.enableJitter);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Desfase espacial con Interleaved Gradient Noise. Elimina el banding por pasos.");

    ImGui::Checkbox("Half-Resolution Rendering (Downsampling)", &settings.halfRes);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Renderiza el pase volumetrico a 0.5x de resolucion y lo recompone sobre la escena solida.");

    ImGui::Checkbox("Mostrar Suelo / Entorno", &settings.showGround);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Muestra el plano de suelo con cuadricula metrica e iluminacion de la fogata.");

    ImGui::Checkbox("Self-Shadowing (Sombras Internas)", &settings.enableSelfShadow);
    if (settings.enableSelfShadow) {
        ImGui::SliderInt("Pasos Sombra Secundaria", &settings.shadowSteps, 2, 8);
    }
}

void DiagnosticUI::renderOpticalParameters(SceneSettings& settings) {
    ImGui::SliderFloat("Absorcion (Sigma_a)", &settings.sigmaA, 0.0f, 20.0f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ley de Beer-Lambert: Atenuacion exponencial exp(-sigma_a * densidad * dt)");

    ImGui::SliderFloat("Dispersion (Sigma_s)", &settings.sigmaS, 0.0f, 20.0f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("In-Scattering: Luz dispersada en direccion hacia la camara");

    ImGui::SliderFloat("Henyey-Greenstein (g)", &settings.henyeyG, -0.8f, 0.9f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("g > 0: Forward scattering (Silver Lining)\ng = 0: Isotropico\ng < 0: Backward scattering");

    if (settings.type == SceneType::CampfireVDB) {
        ImGui::SliderFloat("Emision Blackbody", &settings.emissionIntensity, 0.0f, 10.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Radiacion termica de cuerpo negro segun temperatura normalizada");
    } else if (settings.type == SceneType::ProceduralCloud) {
        ImGui::SliderFloat("Emision Nebulosa", &settings.emissionIntensity, 0.0f, 6.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Intensidad del gas ionizado y nucleo estelar");
    }

    ImGui::SliderFloat("Multiplicador Densidad", &settings.densityMultiplier, 0.1f, 5.0f, "%.2f");
    ImGui::SliderFloat("Exposicion ACES", &settings.exposure, 0.2f, 3.0f, "%.2f");
}

void DiagnosticUI::renderLightControls(SceneSettings& settings) {
    ImGui::SliderFloat3("Posicion Luz (X, Y, Z)", &settings.lightPos.x, -6.0f, 6.0f, "%.2f");
    ImGui::ColorEdit3("Color de Luz", &settings.lightColor.r);
    ImGui::SliderFloat("Intensidad de Luz", &settings.lightIntensity, 0.0f, 10.0f, "%.1f");
    ImGui::Checkbox("Orbita Automatica de Luz", &settings.orbitLight);

    if (settings.type == SceneType::ProceduralCloud) {
        if (ImGui::Button("Luz Trasera (Silver Lining)")) {
            settings.lightPos = glm::vec3(0.0f, 1.55f, -3.0f);
            settings.henyeyG = 0.72f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Luz Lateral (Sombras)")) {
            settings.lightPos = glm::vec3(3.0f, 1.8f, 0.5f);
        }
    } else if (settings.type == SceneType::GodRaysGeometry) {
        ImGui::SliderFloat("Apertura Spotlight", &settings.spotlightCutoff, 15.0f, 60.0f, "%.1f deg");
        if (ImGui::Button("Contraluz (God Rays)")) {
            settings.lightPos = glm::vec3(-2.8f, 3.6f, -2.4f);
            settings.spotlightTarget = glm::vec3(0.4f, 0.4f, 0.4f);
            settings.henyeyG = 0.68f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Luz Cenital")) {
            settings.lightPos = glm::vec3(0.0f, 4.2f, -0.5f);
            settings.spotlightTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        }
    }
}

void DiagnosticUI::renderPlaybackControls(SceneSettings& settings) {
    if (ImGui::Button(settings.isPlaying ? "Pausar Animacion" : "Reproducir Animacion")) {
        settings.isPlaying = !settings.isPlaying;
    }
    ImGui::SameLine();
    ImGui::SliderFloat("Velocidad", &settings.playbackSpeed, 0.1f, 3.0f, "%.1fx");

    if (!settings.isPlaying) {
        ImGui::SliderFloat("Scrubber Frame", &settings.manualFrame, 0.0f, 0.999f, "Frame %.2f");
    }
}

void DiagnosticUI::renderDebugModes(SceneSettings& settings) {
    ImGui::RadioButton("Normal (Render Cinematografico)", &settings.debugMode, 0);
    ImGui::RadioButton("Step Count Heatmap", &settings.debugMode, 1);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Muestra visualmente las iteraciones por pixel:\nAzul Marino: 0 pasos / AABB discard\nVerde/Amarillo: Pocos pasos (Early Exit)\nRojo/Magenta: Max pasos");
    }
    ImGui::RadioButton("Solo Densidad (Monocromo)", &settings.debugMode, 2);

    if (settings.debugMode == 1) {
        ImGui::Text("Leyenda Heatmap:");
        ImGui::TextColored(ImVec4(0.05f, 0.2f, 0.8f, 1.0f), "[Azul Marino]: 0 pasos / Descarte AABB");
        ImGui::TextColored(ImVec4(0.1f, 0.95f, 0.2f, 1.0f), "[Cian - Verde]: Early Ray Termination");
        ImGui::TextColored(ImVec4(1.0f, 0.88f, 0.0f, 1.0f), "[Amarillo]: Muestreo intermedio");
        ImGui::TextColored(ImVec4(1.0f, 0.1f, 0.2f, 1.0f), "[Rojo]: Pasos maximos");
    }
}

} // namespace vrm
