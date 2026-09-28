#include "GuiManager.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glm/gtc/type_ptr.hpp>

void GuiManager::init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void GuiManager::render(EngineState& state, DirectionalLight& light, const Camera& cam) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Engine Controls");

    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("FPS: %.1f (%.3f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
    }

    if (ImGui::CollapsingHeader("Lighting & Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable Shadow Mapping", &state.useShadowMap);
        ImGui::Checkbox("Animate Light Position", &state.animateLight);
        ImGui::DragFloat3("Light Position", glm::value_ptr(light.position), 0.1f);
        ImGui::ColorEdit3("Light Ambient", glm::value_ptr(light.ambient));
        ImGui::ColorEdit3("Light Diffuse", glm::value_ptr(light.diffuse));
        ImGui::ColorEdit3("Light Specular", glm::value_ptr(light.specular));
    }

    if (ImGui::CollapsingHeader("Diagnostic Color Maps")) {
        const char* modes[] = { "0: Default Phong", "1: Unlit Flat Geometry", "2: TexCoords / Geodesic Heatmap" };
        ImGui::Combo("Render Mode", &state.debugMode, modes, IM_ARRAYSIZE(modes));
        if (state.debugMode == 2) {
            ImGui::Checkbox("Interpret UV.x as Heatmap", &state.isHeatmapMode);
        }
    }

    if (ImGui::CollapsingHeader("Vertex Picking Info")) {
        if (state.hasSelection && state.lastPickResult.hit) {
            ImGui::Text("Selected Vertex ID: %d", state.lastPickResult.vertexIndex);
            ImGui::Text("Sub-mesh: %p", static_cast<const void*>(state.lastPickResult.meshAsset.get()));
            ImGui::Text("World Pos: (%.2f, %.2f, %.2f)",
                        state.lastPickResult.vertexWorldPos.x,
                        state.lastPickResult.vertexWorldPos.y,
                        state.lastPickResult.vertexWorldPos.z);
            ImGui::Text("Hit Distance: %.2f", state.lastPickResult.distance);
        } else {
            ImGui::Text("No vertex selected");
        }
    }

    if (ImGui::CollapsingHeader("Camera Info")) {
        ImGui::Text("Pos: (%.2f, %.2f, %.2f)", cam.position.x, cam.position.y, cam.position.z);
        ImGui::Text("Target: (%.2f, %.2f, %.2f)", cam.target.x, cam.target.y, cam.target.z);
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void GuiManager::shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
